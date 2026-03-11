# MEBC_SE/Embarque/src/types.py
from dataclasses import dataclass, field
from typing import Any, Dict, Optional
import time

@dataclass(frozen=True)
class SensorMsg:
    ts: float
    source: str
    data: Dict[str, Any]
    valid: bool = True
    quality: Optional[float] = None
    seq: int = 0
    meta: Dict[str, Any] = field(default_factory=dict)

    @staticmethod
    def make(
        source: str,
        data: Dict[str, Any],
        ts: Optional[float] = None,
        valid: bool = True,
        quality: Optional[float] = None,
        seq: int = 0,
        meta: Optional[Dict[str, Any]] = None,
    ) -> "SensorMsg":
        return SensorMsg(
            ts=time.monotonic() if ts is None else ts,
            source=source,
            data=data,
            valid=valid,
            quality=quality,
            seq=seq,
            meta={} if meta is None else meta,
        )

    def to_dict(self) -> Dict[str, Any]:
        d = {"ts": self.ts, "source": self.source, "data": self.data, "valid": self.valid}
        if self.quality is not None:
            d["quality"] = self.quality
        if self.seq:
            d["seq"] = self.seq
        if self.meta:
            d["meta"] = self.meta
        return d
