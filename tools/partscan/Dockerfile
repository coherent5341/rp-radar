FROM python:3.12-slim

ENV PYTHONUNBUFFERED=1 \
    PYTHONDONTWRITEBYTECODE=1 \
    PARTSCAN_DB=/data/partscan.sqlite3

WORKDIR /app

COPY requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

COPY partscan/ ./partscan/
COPY wsgi.py .

# The database lives on a volume so the parts list outlives the container.
RUN useradd --create-home --uid 10001 partscan \
 && mkdir -p /data \
 && chown -R partscan:partscan /data /app
USER partscan
VOLUME ["/data"]

EXPOSE 8000
HEALTHCHECK --interval=30s --timeout=3s --start-period=5s \
  CMD python -c "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8000/healthz').read()"

# Two workers, one thread each: SQLite is happy with that and the load is a
# person with a scanner.
CMD ["gunicorn", "--bind", "0.0.0.0:8000", "--workers", "2", "--access-logfile", "-", "wsgi:app"]
