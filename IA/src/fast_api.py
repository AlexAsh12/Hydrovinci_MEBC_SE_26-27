from fastapi import FastAPI
from pydantic import BaseModel,Field, ConfigDict
import joblib
import json
from pathlib import Path
import pandas as pd
app=FastAPI()
class model_input(BaseModel):
    model_config = ConfigDict(populate_by_name=True)
    Speed_kmh : float
    Acceleration_ms2 : float
    Battery_State : float  = Field(alias="Battery_State_%")
    Battery_Voltage_V : float
    Battery_Temperature_C : float
    Humidity : float = Field(alias="Humidity_%")
    Wind_Speed_ms : float
    Vehicle_Weight_kg: float

#loading the model
BASE_DIR = Path(__file__).resolve().parent.parent 
MODEL_PATH = BASE_DIR / "artifacts" / "models" / "ML" / "ElasticNet_model.joblib"
model = joblib.load(MODEL_PATH)
@app.post('/consumption_prediction')
def consumption_pred(input: model_input):
    input_dict = input.model_dump(by_alias=True)#transform input data into python dictionnary
    df = pd.DataFrame([input_dict])#tranform to dataframe
    prediction = model.predict(df)#makes the prediction 
    return {"prediction": float(prediction[0])}

@app.get("/health")
def health():
    return {"status": "ok"}








