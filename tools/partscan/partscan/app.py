"""
The web front end: a scan box, a parts list, and a page per component.

Run it with `flask --app partscan.app run`, or in Docker with the gunicorn
entry point in wsgi.py.
"""

from __future__ import annotations

import csv
import io
import json
import os
import sqlite3
from typing import Any, Dict, Optional

from flask import (Flask, Response, abort, flash, g, jsonify, redirect,
                   render_template, request, url_for)

from . import classify, db
from .barcode import BarcodeError, describe_date_code, parse


def create_app(database: Optional[str] = None) -> Flask:
    app = Flask(__name__)
    app.config["DATABASE"] = database or os.environ.get("PARTSCAN_DB", db.DEFAULT_DB)
    app.config["SECRET_KEY"] = os.environ.get("PARTSCAN_SECRET", os.urandom(24).hex())

    # ---- database handle, one per request ------------------------------
    def store() -> sqlite3.Connection:
        if "db" not in g:
            g.db = db.connect(app.config["DATABASE"])
        return g.db

    @app.teardown_appcontext
    def close_store(_exception: Optional[BaseException]) -> None:
        connection = g.pop("db", None)
        if connection is not None:
            connection.close()

    @app.template_filter("timestamp")
    def _timestamp(value: str) -> str:
        """2026-08-23T09:31:07+00:00 reads better as 2026-08-23 09:31."""
        if not value:
            return ""
        return value.replace("T", " ")[:16]

    @app.template_filter("date_code")
    def _date_code(value: str) -> str:
        return describe_date_code(value)

    @app.template_filter("fromjson")
    def _fromjson(value: str) -> Dict[str, Any]:
        """The fields a supplier prints that we deliberately do not name."""
        try:
            return json.loads(value or "{}")
        except ValueError:
            return {}

    # ---- pages ----------------------------------------------------------
    @app.get("/")
    def index() -> str:
        sort = request.args.get("sort", db.DEFAULT_SORT)
        if sort not in db.SORTS:
            sort = db.DEFAULT_SORT
        descending = request.args.get("dir", "asc") == "desc"
        category = request.args.get("category", "")
        query = request.args.get("q", "").strip()

        return render_template(
            "index.html",
            components=db.components(store(), sort, descending, category, query),
            categories=db.categories(store()),
            totals=db.totals(store()),
            sorts=db.SORTS,
            sort=sort,
            direction="desc" if descending else "asc",
            category=category,
            query=query,
        )

    @app.get("/component/<int:component_id>")
    def component(component_id: int) -> str:
        row = db.component(store(), component_id)
        if row is None:
            abort(404)
        return render_template(
            "component.html",
            component=row,
            scans=db.scans(store(), component_id),
            categories=classify.CATEGORIES,
        )

    # ---- actions --------------------------------------------------------
    @app.post("/scan")
    def scan():
        text = request.form.get("barcode", "")
        force = request.form.get("force") == "on"
        back = request.form.get("next") or url_for("index")
        try:
            result = _store_scan(store(), text, force=force)
        except BarcodeError as error:
            flash(f"Could not read that barcode: {error}", "error")
            return redirect(back)

        if result["duplicate"]:
            flash(
                "That exact label is already recorded — nothing added. "
                "Tick 'record duplicates' if you really do have two of it.",
                "warning",
            )
            return redirect(back)

        code = result["barcode"]
        flash(f"Recorded {code.quantity} x {code.part} from {code.supplier}", "ok")
        return redirect(back)

    @app.post("/component/<int:component_id>/category")
    def recategorise(component_id: int):
        category = request.form.get("category", "")
        if not db.set_category(store(), component_id, category):
            flash(f"'{category}' is not a category.", "error")
        return redirect(url_for("component", component_id=component_id))

    @app.post("/scan/<int:scan_id>/delete")
    def delete_scan(scan_id: int):
        component_id = db.delete_scan(store(), scan_id)
        if component_id is None:
            abort(404)
        flash("Scan deleted.", "ok")
        if component_id:
            return redirect(url_for("component", component_id=component_id))
        return redirect(url_for("index"))

    @app.post("/component/<int:component_id>/delete")
    def delete_component(component_id: int):
        if not db.delete_component(store(), component_id):
            abort(404)
        flash("Component and its scans deleted.", "ok")
        return redirect(url_for("index"))

    # ---- machine-readable ----------------------------------------------
    @app.post("/api/scan")
    def api_scan():
        payload: Dict[str, Any] = request.get_json(silent=True) or {}
        text = payload.get("barcode") or request.form.get("barcode", "")
        force = bool(payload.get("force", False))
        try:
            result = _store_scan(store(), text, force=force)
        except BarcodeError as error:
            return jsonify({"error": str(error)}), 400

        code = result["barcode"]
        body = {
            "duplicate": result["duplicate"],
            "component_id": result["component_id"],
            "scan_id": result["scan_id"],
            "barcode": code.as_dict(),
            "component": classify.classify(
                code.get("mfr_part"), code.get("supplier_part"), code.supplier
            ).as_dict(),
        }
        return jsonify(body), 200 if result["duplicate"] else 201

    @app.get("/api/components")
    def api_components():
        rows = db.components(
            store(),
            request.args.get("sort", db.DEFAULT_SORT),
            request.args.get("dir", "asc") == "desc",
            request.args.get("category", ""),
            request.args.get("q", "").strip(),
        )
        return jsonify(db.rows_to_dicts(rows))

    @app.get("/api/parse")
    def api_parse():
        """Decode without storing — handy when checking a label by hand."""
        try:
            return jsonify(parse(request.args.get("barcode", "")).as_dict())
        except BarcodeError as error:
            return jsonify({"error": str(error)}), 400

    @app.get("/export.csv")
    def export_csv() -> Response:
        rows = db.components(
            store(),
            request.args.get("sort", db.DEFAULT_SORT),
            request.args.get("dir", "asc") == "desc",
            request.args.get("category", ""),
            request.args.get("q", "").strip(),
        )
        columns = ["category", "package", "value_text", "mfr_part", "supplier",
                   "supplier_part", "customer_part", "detail", "total_quantity",
                   "scan_count", "last_scan"]
        buffer = io.StringIO()
        writer = csv.writer(buffer)
        writer.writerow(columns)
        for row in rows:
            writer.writerow([row[column] for column in columns])
        return Response(
            buffer.getvalue(),
            mimetype="text/csv",
            headers={"Content-Disposition": "attachment; filename=partscan.csv"},
        )

    @app.get("/healthz")
    def healthz():
        store().execute("SELECT 1")
        return {"status": "ok"}

    return app


def _store_scan(conn: sqlite3.Connection, text: str, force: bool = False) -> Dict[str, Any]:
    """Decode and store one label. Raises BarcodeError if it will not read."""
    code = parse(text)
    if not force:
        existing = db.find_scan_by_raw(conn, code.raw)
        if existing:
            return {
                "duplicate": True,
                "barcode": code,
                "component_id": int(existing["component_id"]),
                "scan_id": int(existing["id"]),
            }
    component_id, scan_id = db.record(conn, code)
    return {"duplicate": False, "barcode": code, "component_id": component_id, "scan_id": scan_id}


app = create_app()
