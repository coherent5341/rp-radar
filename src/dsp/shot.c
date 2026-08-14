#include "shot.h"

#include <math.h>
#include <string.h>

/* A legal driver tops out near 1.5; anything past this is a bad measurement. */
#define SMASH_PLAUSIBLE_MIN 0.8f
#define SMASH_PLAUSIBLE_MAX 1.6f

void shot_default_config(shot_cfg_t *cfg)
{
	cfg->trigger_speed_mps = 12.0f;
	cfg->min_snr           = 10.0f;
	cfg->capture_ms        = 250u;
	cfg->cooldown_ms       = 800u;
	cfg->ball_window_ms    = 60u;
	cfg->impact_jump_ratio = 1.25f;
}

void shot_init(shot_t *shot, const shot_cfg_t *cfg)
{
	memset(shot, 0, sizeof(*shot));
	shot->cfg     = *cfg;
	shot->state   = SHOT_STATE_IDLE;
	shot->next_id = 1u;
}

shot_state_t shot_state(const shot_t *shot)
{
	return shot->state;
}

/*
 * Reduce a frame's peaks to the two that matter: the fastest, and the fastest
 * of the rest. Ordering by speed rather than by power is what lets the ball be
 * seen at all — right after impact the club head is the stronger return, but
 * the ball is the faster one.
 */
static void reduce_peaks(const shot_t *shot, const peak_t *peaks, uint16_t num_peaks,
                         const psd_t *psd, shot_sample_t *sample)
{
	sample->fast_speed_mps = 0.0f;
	sample->fast_snr       = 0.0f;
	sample->slow_speed_mps = 0.0f;
	sample->slow_snr       = 0.0f;

	for (uint16_t i = 0; i < num_peaks; i++)
	{
		if (peaks[i].snr < shot->cfg.min_snr)
		{
			continue;
		}

		const float speed = psd_bin_to_speed(psd, peaks[i].bin);
		const float mag   = fabsf(speed);

		if (mag > fabsf(sample->fast_speed_mps))
		{
			sample->slow_speed_mps = sample->fast_speed_mps;
			sample->slow_snr       = sample->fast_snr;
			sample->fast_speed_mps = speed;
			sample->fast_snr       = peaks[i].snr;
		}
		else if (mag > fabsf(sample->slow_speed_mps))
		{
			sample->slow_speed_mps = speed;
			sample->slow_snr       = peaks[i].snr;
		}
	}
}

static void analyse(shot_t *shot, shot_result_t *out)
{
	const uint16_t n = shot->num_samples;

	memset(out, 0, sizeof(*out));
	out->num_samples = n;

	if (n == 0u)
	{
		return;
	}

	out->duration_ms = shot->samples[n - 1u].t_ms - shot->samples[0].t_ms;

	/* Largest frame-to-frame jump in peak speed marks the impact. */
	uint16_t impact      = 0u;
	float    best_ratio  = 0.0f;
	bool     found_jump  = false;

	for (uint16_t i = 1u; i < n; i++)
	{
		const float previous = fabsf(shot->samples[i - 1u].fast_speed_mps);
		const float current  = fabsf(shot->samples[i].fast_speed_mps);

		if (previous <= 0.0f)
		{
			continue;
		}

		const float ratio = current / previous;

		if (ratio >= shot->cfg.impact_jump_ratio && ratio > best_ratio)
		{
			best_ratio = ratio;
			impact     = i;
			found_jump = true;
		}
	}

	if (!found_jump)
	{
		/*
		 * No clean step. That happens on a mishit, or when the ball is masked
		 * by the club. Fall back to the frame with the highest speed and treat
		 * everything before it as the downswing.
		 */
		float fastest = 0.0f;

		for (uint16_t i = 0; i < n; i++)
		{
			const float mag = fabsf(shot->samples[i].fast_speed_mps);

			if (mag > fastest)
			{
				fastest = mag;
				impact  = i;
			}
		}
	}

	out->t_impact_ms = shot->samples[impact].t_ms;

	/* Club: fastest thing seen before the step. */
	float club = 0.0f;

	for (uint16_t i = 0; i < impact; i++)
	{
		const float mag = fabsf(shot->samples[i].fast_speed_mps);

		if (mag > club)
		{
			club = mag;
		}
	}

	/* Ball: fastest thing inside the window after the step. */
	float    ball        = 0.0f;
	float    snr_sum     = 0.0f;
	uint16_t window_used = 0u;

	for (uint16_t i = impact; i < n; i++)
	{
		if ((shot->samples[i].t_ms - out->t_impact_ms) > shot->cfg.ball_window_ms)
		{
			break;
		}

		const float mag = fabsf(shot->samples[i].fast_speed_mps);

		if (mag > ball)
		{
			ball = mag;
		}

		/*
		 * The club is still in the beam just after impact, now as the slower
		 * peak. If the downswing was never seen — the trigger can fire late on
		 * a fast swing — this recovers a club speed that would otherwise be
		 * missing.
		 */
		const float slow = fabsf(shot->samples[i].slow_speed_mps);

		if (club <= 0.0f && slow > 0.0f && slow > club)
		{
			club = slow;
		}

		snr_sum += shot->samples[i].fast_snr;
		window_used++;
	}

	out->valid          = ball > 0.0f;
	out->club_speed_mps = club;
	out->ball_speed_mps = ball;
	out->smash_factor   = (club > 0.0f) ? (ball / club) : 0.0f;

	if (out->smash_factor > 0.0f &&
	    (out->smash_factor < SMASH_PLAUSIBLE_MIN || out->smash_factor > SMASH_PLAUSIBLE_MAX))
	{
		out->implausible = true;
	}

	/*
	 * Confidence blends how well the ball window was populated with how strong
	 * the returns in it were, then halves the result when the smash factor is
	 * outside what a golf club can physically do.
	 */
	float confidence = 0.0f;

	if (window_used > 0u)
	{
		const float mean_snr = snr_sum / (float)window_used;
		const float snr_term = (mean_snr >= (shot->cfg.min_snr * 4.0f))
		                           ? 1.0f
		                           : (mean_snr / (shot->cfg.min_snr * 4.0f));
		const float fill_term = (window_used >= 4u) ? 1.0f : ((float)window_used / 4.0f);

		confidence = snr_term * fill_term;
	}

	if (out->implausible)
	{
		confidence *= 0.5f;
	}
	if (club <= 0.0f)
	{
		confidence *= 0.5f;
	}

	out->confidence = confidence;
	out->id         = shot->next_id;
}

bool shot_push(shot_t *shot, uint32_t t_ms, const peak_t *peaks, uint16_t num_peaks,
               const psd_t *psd, shot_result_t *out)
{
	shot_sample_t sample;

	sample.t_ms = t_ms;
	reduce_peaks(shot, peaks, num_peaks, psd, &sample);

	const float fast_mag = fabsf(sample.fast_speed_mps);

	switch (shot->state)
	{
		case SHOT_STATE_IDLE:
			if (fast_mag >= shot->cfg.trigger_speed_mps)
			{
				shot->state       = SHOT_STATE_CAPTURE;
				shot->t_state_ms  = t_ms;
				shot->num_samples = 0u;
				shot->samples[shot->num_samples++] = sample;
			}
			break;

		case SHOT_STATE_CAPTURE:
			if (shot->num_samples < SHOT_MAX_SAMPLES)
			{
				shot->samples[shot->num_samples++] = sample;
			}

			if ((t_ms - shot->t_state_ms) >= shot->cfg.capture_ms ||
			    shot->num_samples >= SHOT_MAX_SAMPLES)
			{
				analyse(shot, out);
				shot->next_id++;
				shot->state      = SHOT_STATE_COOLDOWN;
				shot->t_state_ms = t_ms;
				return true;
			}
			break;

		case SHOT_STATE_COOLDOWN:
			if ((t_ms - shot->t_state_ms) >= shot->cfg.cooldown_ms &&
			    fast_mag < shot->cfg.trigger_speed_mps)
			{
				shot->state       = SHOT_STATE_IDLE;
				shot->t_state_ms  = t_ms;
				shot->num_samples = 0u;
			}
			break;

		default:
			shot->state = SHOT_STATE_IDLE;
			break;
	}

	return false;
}
