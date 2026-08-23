import os
import sys

import pytest

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

from partscan.app import create_app  # noqa: E402

# The label from a reel of Yageo 8.2 pF 0402 capacitors, as a keyboard-wedge
# scanner types it: the separators are gone.
SAMPLE = (
    "[)>06PC50,C511PCC0402BPNPO9BN8R230P13-CC0402BPNPO9BN8R2CT-NDK1K10027327210K1288457209D2336"
    "1T89M333603911K14LTWQ1011ZPICK12Z2128519113Z99999920Z000000000000000000000000000000000000"
    "00000000000000000000000000000000000000"
)


@pytest.fixture
def app(tmp_path):
    application = create_app(database=str(tmp_path / "test.sqlite3"))
    application.config.update(TESTING=True, SECRET_KEY="test")
    return application


@pytest.fixture
def client(app):
    return app.test_client()
