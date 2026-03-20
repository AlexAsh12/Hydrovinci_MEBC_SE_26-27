# MEBC_SE/Embarque/src/format_ai.py
from typing import Dict, Any, Optional
from .types import SensorMsg

# ---------------------------
# 1) Conversions "raw -> IA"
# ---------------------------

def bms_to_ai(bms: SensorMsg) -> Dict[str, Any]:
    """Convertit un message BMS brut vers les features IA batterie."""
    if (bms is None) or (not bms.valid):
        return {}

    d = bms.data
    return {
        "Battery_Voltage_V": d.get("voltage"),
        "Battery_Temperature_C": d.get("temps"),
        "Battery_State_%": d.get("soc"),
    }


def gps_to_ai(gps: SensorMsg) -> Dict[str, Any]:
    """GPS brut -> Speed_kmh (et autres si tu veux)."""
    if (gps is None) or (not gps.valid):
        return {}
    d = gps.data
    return {"Speed_kmh": d.get("speed_kmh")}


def imu_to_ai(imu: SensorMsg) -> Dict[str, Any]:
    """IMU brut -> Acceleration_ms2 (selon ton driver)."""
    if (imu is None) or (not imu.valid):
        return {}
    d = imu.data
    return {"Acceleration_ms2": d.get("acc_ms2")}


def meteo_to_ai(meteo: SensorMsg) -> Dict[str, Any]:
    """Meteo brut -> Humidity_% / Wind_Speed_ms."""
    if (meteo is None) or (not meteo.valid):
        return {}
    d = meteo.data
    return {
        "Humidity_%": d.get("humidity"),
        "Wind_Speed_ms": d.get("wind_speed"),
    }


def build_ai_features(
    *,
    bms: Optional[SensorMsg] = None,
    gps: Optional[SensorMsg] = None,
    imu: Optional[SensorMsg] = None,
    meteo: Optional[SensorMsg] = None,
    vehicle_weight_kg: Optional[float] = None,
) -> Dict[str, Any]:
    """
    Assemble toutes les sources pour produire exactement le format attendu par le modèle IA.
    """
    features: Dict[str, Any] = {}
    features.update(bms_to_ai(bms) if bms else {})
    features.update(gps_to_ai(gps) if gps else {})
    features.update(imu_to_ai(imu) if imu else {})
    features.update(meteo_to_ai(meteo) if meteo else {})

    if vehicle_weight_kg is not None:
        features["Vehicle_Weight_kg"] = float(vehicle_weight_kg)

    # nettoie les None
    return {k: v for k, v in features.items() if v is not None}


# ---------------------------
# 2) EMA smoothing (ton code)
# ---------------------------

class EMA:
    def __init__(self, alpha: float):
        self.alpha = float(alpha)
        self.value: Optional[float] = None

    def update(self, new_value: float) -> float:
        new_value = float(new_value)
        if self.value is None:
            self.value = new_value
        else:
            self.value = self.alpha * new_value + (1 - self.alpha) * self.value
        return self.value


EMA_FILTERS: Dict[str, EMA] = {
    "Speed_kmh": EMA(0.10),
    "Acceleration_ms2": EMA(0.20),
    "Battery_State_%": EMA(0.05),
    "Battery_Voltage_V": EMA(0.15),
    "Battery_Temperature_C": EMA(0.10),
    "Humidity_%": EMA(0.10),
    "Wind_Speed_ms": EMA(0.10),
}

PASS_THROUGH_KEYS = {"Vehicle_Weight_kg"}


def update_sliding_ema(data: Dict[str, Any]) -> Dict[str, float]:
    """
    Lisse les features via EMA. Les constantes sont pass-through.
    """
    smoothed: Dict[str, float] = {}

    for key, filt in EMA_FILTERS.items():
        if key in data and data[key] is not None:
            smoothed[key] = filt.update(data[key])

    for key in PASS_THROUGH_KEYS:
        if key in data and data[key] is not None:
            smoothed[key] = float(data[key])

    return smoothed
