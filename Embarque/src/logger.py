import json
import time
import logging
from logging.handlers import RotatingFileHandler
from pathlib import Path
from typing import Union
from .types import SensorMsg


class JsonLogger:
    def __init__(
        self,
        datapath: Union[str, Path],
        errorpath: Union[str, Path] = None,
        max_bytes: int = 5_000_000,
        backup_count: int = 3,
    ):
        base_dir = Path(__file__).resolve().parent.parent
        log_dir = base_dir / "log"
        log_dir.mkdir(exist_ok=True)

        self.datapath = Path(datapath)
        self.errorpath = Path(errorpath) if errorpath else (log_dir / "error.jsonl")

        # 1) Logger "données"
        self.data_logger = logging.getLogger("mebc.data")
        self.data_logger.setLevel(logging.INFO)
        self.data_logger.propagate = False

        # 2) Logger "erreurs"
        self.err_logger = logging.getLogger("mebc.error")
        self.err_logger.setLevel(logging.INFO)
        self.err_logger.propagate = False

        # 3) Handlers de rotation (le truc important)
        if not self.data_logger.handlers:
            data_handler = RotatingFileHandler(
                self.datapath,
                maxBytes=max_bytes,
                backupCount=backup_count,
                encoding="utf-8",
            )
            data_handler.setFormatter(logging.Formatter("%(message)s"))
            self.data_logger.addHandler(data_handler)

        if not self.err_logger.handlers:
            err_handler = RotatingFileHandler(
                self.errorpath,
                maxBytes=max_bytes,
                backupCount=backup_count,
                encoding="utf-8",
            )
            err_handler.setFormatter(logging.Formatter("%(message)s"))
            self.err_logger.addHandler(err_handler)

    def log(self, msg: SensorMsg):
        try:
            self.data_logger.info(json.dumps(msg.to_dict(), separators=(",", ":")))
        except Exception as e:
            self.err_logger.info(json.dumps({
                "time": time.time(),
                "error": type(e).__name__,
                "message": str(e),
            }, separators=(",", ":")))

    def close(self):
        # Important: fermer les handlers pour flush + libérer le fichier
        for logger in (self.data_logger, self.err_logger):
            for h in list(logger.handlers):
                h.close()
                logger.removeHandler(h)
