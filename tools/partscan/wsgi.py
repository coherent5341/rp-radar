"""Entry point for gunicorn: `gunicorn wsgi:app`."""

from partscan.app import create_app

app = create_app()
