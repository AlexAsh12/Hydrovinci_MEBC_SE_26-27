import json 
from src.logger import JsonLogger
from src.types import SensorMsg
import pytest
import typing 
from pathlib import Path
def test_log_writes_message(tmp_path : Path):
    log_file = tmp_path / "log.jsonl"
    error_file = tmp_path / "error.jsonl"

    logger = JsonLogger(log_file, error_file)

    msg = SensorMsg.make(
        source="gps",
        data={"lat": 1.0, "lon": 2.0}
    )

    logger.log(msg)
    logger.close()

    assert log_file.exists()
    lines = log_file.read_text().splitlines()
    assert len(lines) == 1

    parsed = json.loads(lines[0])
    assert parsed["source"] == "gps"
    assert parsed["data"]["lat"] == 1.0
    assert parsed["data"]["lon"] == 2.0 

