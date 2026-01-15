from src.fast_api import app
from fastapi.testclient import TestClient
import random 
client=TestClient(app)

def test_health():
    """
    Asses whether the api works or not
    """
    r=client.get("/health")
    assert r.status_code == 200 #corresponds to a functional status code
    assert r.json() == {"status" : "ok"}
def test_consumption_prediction_ok():
    """
    Assert that the model returns a prediction
    """
    payload = {
        "Speed_kmh": 50,
        "Acceleration_ms2": 1.2,
        "Battery_State_%": 80,
        "Battery_Voltage_V": 48,
        "Battery_Temperature_C": 25,
        "Humidity_%": 60,
        "Wind_Speed_ms": 3,
        "Vehicle_Weight_kg": 1200
    }
    r = client.post("/consumption_prediction", json=payload)
    assert r.status_code == 200
    body = r.json()
    assert "prediction" in body
    assert isinstance(body["prediction"], (int, float))

def test_consumption_prediction_validation_error():
    """
    assert that the predictions isn't made when some features are null
    """

    # manque des champs -> FastAPI/Pydantic doit répondre 422
    payload = {"Speed_kmh": 50}
    r = client.post("/consumption_prediction", json=payload)
    assert r.status_code == 422

def test_relevancy_prediction():
    
    payload = {
        "Speed_kmh": random.uniform(0,100),
        "Acceleration_ms2": random.uniform(-10,10),
        "Battery_State_%": random.uniform(0,100),
        "Battery_Voltage_V": random.uniform(10,100),
        "Battery_Temperature_C": random.uniform(10,100),
        "Humidity_%": random.uniform(0,100),
        "Wind_Speed_ms": random.uniform(5,100),
        "Vehicle_Weight_kg": random.uniform(500,1000)
    }
    r=client.post("/consumption_prediction",json=payload)
    data = r.json() # get the answer from the api in a dictionnary format 
    assert "prediction" in data 
    assert isinstance(data["prediction"], (int, float))
    assert data["prediction"] > 0 



