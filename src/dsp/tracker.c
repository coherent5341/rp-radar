#include "tracker.h"

#include <math.h>
#include <string.h>

void tracker_default_config(tracker_cfg_t *cfg)
{
	cfg->gate_m             = 0.25f;
	cfg->alpha              = 0.6f;
	cfg->beta               = 0.7f;
	cfg->amp_alpha          = 0.4f;
	cfg->max_misses         = 6u;
	cfg->min_hits           = 2u;
	cfg->ball_min_speed_mps = 15.0f;
	cfg->ball_max_amp_ratio = 0.5f;
}

void tracker_init(tracker_t *tracker, const tracker_cfg_t *cfg)
{
	memset(tracker, 0, sizeof(*tracker));
	tracker->cfg     = *cfg;
	tracker->next_id = 1u;
}

static float speed_measurement(const tracker_t *tracker, const track_t *track,
                               const rd_det_t *det, float max_speed_mps,
                               float coarse_speed_mps, bool coarse_valid)
{
	(void)tracker;

	if (max_speed_mps <= 0.0f)
	{
		return det->speed_mps;
	}

	/*
	 * An existing track is the strongest possible guide: its speed is a
	 * physically continuous quantity, so the correct alias is the one nearest
	 * to it. Fall back to the frame's range-walk estimate for new tracks, and
	 * to the folded value when neither is available.
	 */
	if (track != NULL && track->hits > 0u)
	{
		return rd_dealias(det->speed_mps, max_speed_mps, track->speed_mps);
	}

	if (coarse_valid)
	{
		return rd_dealias(det->speed_mps, max_speed_mps, coarse_speed_mps);
	}

	return det->speed_mps;
}

static void classify(tracker_t *tracker)
{
	float strongest = 0.0f;

	for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
	{
		const track_t *t = &tracker->tracks[i];

		if (t->active && t->amp > strongest)
		{
			strongest = t->amp;
		}
	}

	for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
	{
		track_t *t = &tracker->tracks[i];

		if (!t->active)
		{
			continue;
		}

		const float speed = fabsf(t->speed_mps);

		/*
		 * Heuristic, and deliberately a simple one: the club head is the large
		 * reflector, the ball is the small fast one. Amplitudes depend on
		 * geometry and on where the sensor sits, so treat these as a starting
		 * point to tune against your own recordings rather than as absolutes.
		 */
		if (strongest > 0.0f && t->amp <= (strongest * tracker->cfg.ball_max_amp_ratio) &&
		    speed >= tracker->cfg.ball_min_speed_mps)
		{
			t->cls = TRACK_CLASS_BALL;
		}
		else if (t->amp >= (strongest * 0.5f))
		{
			t->cls = TRACK_CLASS_CLUB;
		}
		else
		{
			t->cls = TRACK_CLASS_UNKNOWN;
		}
	}
}

void tracker_update(tracker_t *tracker, const rd_det_t *dets, uint16_t num_dets,
                    float dt_s, uint32_t t_ms, float max_speed_mps,
                    float coarse_speed_mps, bool coarse_valid)
{
	bool det_used[RD_MAX_POINTS * PEAKS_MAX];
	bool track_updated[TRACKER_MAX_TRACKS];

	const uint16_t capped = (num_dets < (uint16_t)(RD_MAX_POINTS * PEAKS_MAX))
	                            ? num_dets
	                            : (uint16_t)(RD_MAX_POINTS * PEAKS_MAX);

	memset(det_used, 0, sizeof(bool) * capped);
	memset(track_updated, 0, sizeof(track_updated));

	/* Predict. */
	for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
	{
		track_t *t = &tracker->tracks[i];

		if (t->active)
		{
			t->range_m += t->speed_mps * dt_s;
		}
	}

	/*
	 * Greedy association, strongest detection first. Detections arrive sorted
	 * by power, so the club head claims its track before a sidelobe can.
	 */
	for (uint16_t d = 0; d < capped; d++)
	{
		int      best      = -1;
		float    best_dist = tracker->cfg.gate_m;

		for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
		{
			const track_t *t = &tracker->tracks[i];

			if (!t->active || track_updated[i])
			{
				continue;
			}

			const float dist = fabsf(dets[d].range_m - t->range_m);

			if (dist <= best_dist)
			{
				best_dist = dist;
				best      = (int)i;
			}
		}

		if (best < 0)
		{
			continue;
		}

		track_t    *t     = &tracker->tracks[best];
		const float v_meas =
		    speed_measurement(tracker, t, &dets[d], max_speed_mps, coarse_speed_mps, coarse_valid);
		const float residual = dets[d].range_m - t->range_m;

		t->range_m += tracker->cfg.alpha * residual;
		t->speed_mps += tracker->cfg.beta * (v_meas - t->speed_mps);
		t->amp += tracker->cfg.amp_alpha * (dets[d].power - t->amp);
		t->hits++;
		t->misses   = 0u;
		t->t_last_ms = t_ms;

		if (fabsf(t->speed_mps) > fabsf(t->peak_speed_mps))
		{
			t->peak_speed_mps = t->speed_mps;
		}

		track_updated[best] = true;
		det_used[d]         = true;
	}

	/* Age out tracks that got nothing this frame. */
	for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
	{
		track_t *t = &tracker->tracks[i];

		if (!t->active || track_updated[i])
		{
			continue;
		}

		t->misses++;
		if (t->misses > tracker->cfg.max_misses)
		{
			memset(t, 0, sizeof(*t));
		}
	}

	/* Spawn tracks for leftovers, evicting the weakest track if all slots are busy. */
	for (uint16_t d = 0; d < capped; d++)
	{
		if (det_used[d])
		{
			continue;
		}

		int   slot      = -1;
		float weakest   = 0.0f;
		bool  found_free = false;

		for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
		{
			if (!tracker->tracks[i].active)
			{
				slot       = (int)i;
				found_free = true;
				break;
			}
		}

		if (!found_free)
		{
			for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
			{
				if (slot < 0 || tracker->tracks[i].amp < weakest)
				{
					weakest = tracker->tracks[i].amp;
					slot    = (int)i;
				}
			}

			/* Do not displace a stronger track with a weaker detection. */
			if (slot >= 0 && tracker->tracks[slot].amp >= dets[d].power)
			{
				continue;
			}
		}

		if (slot < 0)
		{
			continue;
		}

		track_t *t = &tracker->tracks[slot];

		memset(t, 0, sizeof(*t));
		t->active     = true;
		t->id         = tracker->next_id++;
		t->range_m    = dets[d].range_m;
		t->speed_mps  = speed_measurement(tracker, NULL, &dets[d], max_speed_mps,
		                                  coarse_speed_mps, coarse_valid);
		t->amp        = dets[d].power;
		t->hits       = 1u;
		t->misses     = 0u;
		t->t_first_ms = t_ms;
		t->t_last_ms  = t_ms;
		t->peak_speed_mps = t->speed_mps;
		t->cls        = TRACK_CLASS_UNKNOWN;

		if (tracker->next_id == 0u)
		{
			tracker->next_id = 1u;
		}
	}

	classify(tracker);
}

uint16_t tracker_confirmed(const tracker_t *tracker, const track_t **out, uint16_t max_out)
{
	uint16_t count = 0u;

	for (uint16_t i = 0; i < TRACKER_MAX_TRACKS; i++)
	{
		const track_t *t = &tracker->tracks[i];

		if (!t->active || t->hits < tracker->cfg.min_hits)
		{
			continue;
		}

		uint16_t pos = count;

		while (pos > 0u && out[pos - 1u]->amp < t->amp)
		{
			if (pos < max_out)
			{
				out[pos] = out[pos - 1u];
			}
			pos--;
		}

		if (pos < max_out)
		{
			out[pos] = t;
			if (count < max_out)
			{
				count++;
			}
		}
	}

	return count;
}
