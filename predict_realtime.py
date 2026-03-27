import os
import json
import websocket
import threading
import joblib
import pandas as pd
import numpy as np
import pusher
from sklearn.preprocessing import StandardScaler
from dotenv import load_dotenv
from twilio.rest import Client

# Load env
load_dotenv()

TWILIO_ACCOUNT_SID = os.getenv('TWILIO_ACCOUNT_SID')
TWILIO_AUTH_TOKEN = os.getenv('TWILIO_AUTH_TOKEN')
TWILIO_PHONE_NUMBER = os.getenv('TWILIO_PHONE_NUMBER')
RECIPIENT_PHONE_NUMBER = os.getenv('RECIPIENT_PHONE_NUMBER')


PUSHER_KEY = os.getenv('PUSHER_KEY')
PUSHER_CLUSTER = os.getenv('PUSHER_CLUSTER')
CHANNEL_NAME = os.getenv('CHANNEL_NAME')
EVENT_NAME = os.getenv('EVENT_NAME')
MODEL_DIR = os.getenv("MODEL_DIR", "models")
CSV_PATH = os.getenv("CSV_PATH", "data.csv")
RESULT_EVENT = os.getenv("RESULT_EVENT_NAME", "predict-result")


# === Load models ===
models = {
    "svm": joblib.load(f"{MODEL_DIR}/svm_model.pkl"),
    "logreg": joblib.load(f"{MODEL_DIR}/logreg_model.pkl"),
    "rf": joblib.load(f"{MODEL_DIR}/rf_model.pkl"),
}
print("✅ Models loaded.")

# === Pusher connect ===
pusher_rest = pusher.Pusher(
    app_id=os.getenv("PUSHER_APP_ID"),
    key=os.getenv("PUSHER_KEY"),
    secret=os.getenv("PUSHER_SECRET"),
    cluster=os.getenv("PUSHER_CLUSTER"),
    ssl=True
)

# === Fit scaler from original data ===
df = pd.read_csv(CSV_PATH)
X = df.drop("outcome", axis=1)
y = df["outcome"]

scaler = StandardScaler()
scaler.fit(X)

# === Send SMS result ===
twilio_client = Client(TWILIO_ACCOUNT_SID, TWILIO_AUTH_TOKEN)
def send_sms(message):
    print(f"📱 Sending SMS: {message}")
    try:
        message = twilio_client.messages.create(
            body=message,
            from_=TWILIO_PHONE_NUMBER,
            to=RECIPIENT_PHONE_NUMBER
        )
        print(f"📱 SMS sent: {message.sid}")
    except Exception as e:
        print(f"❌ Failed to send SMS: {e}")


# === Send prediction result ===
def send_result_event(results):
    try:
        pusher_rest.trigger(CHANNEL_NAME, RESULT_EVENT, results)
        print(f"📤 Sent prediction result to `{RESULT_EVENT}` event.")

        # Check RF prediction
        rf_result = results.get("rf", {}).get("predict", None)

        if rf_result == 1:
            send_sms("Incoming bloom detected.")
        elif rf_result == 0:
            send_sms("Cyanobacteria concentration is now within normal range.")
        else:
            print("ℹ️ RF prediction not in expected category; no SMS sent.")

    except Exception as e:
        print(f"❌ Failed to send result: {e}")

# === Prediction handler ===
def predict_all_models(data):
    df_input = pd.DataFrame([data])
    X_scaled = scaler.transform(df_input)

    results = {}
    for name, model in models.items():
        pred = model.predict(X_scaled)[0]
        if hasattr(model, "predict_proba"):
            score = np.max(model.predict_proba(X_scaled))
        elif hasattr(model, "decision_function"):
            raw = model.decision_function(X_scaled)[0]
            score = float(1 / (1 + np.exp(-raw)))
        else:
            score = None

        results[name] = {
            "predict": int(pred),
            "score": round(score, 4) if score is not None else None,
        }

    print("📊 Prediction Result:", json.dumps(results, indent=2))
    return results

def on_message(ws, message):
    message_data = json.loads(message)

    event = message_data.get('event')
    if event == 'pusher:connection_established':
        socket_data = json.loads(message_data['data'])
        socket_id = socket_data['socket_id']
        print(f"Connected with socket_id: {socket_id}")

        # Subscribe to the desired channel
        subscribe_payload = {
            'event': 'pusher:subscribe',
            'data': {
                'channel': CHANNEL_NAME
            }
        }
        ws.send(json.dumps(subscribe_payload))

    elif event == EVENT_NAME:
        print(f"Received event `{EVENT_NAME}` data: {message_data['data']}")
        results = predict_all_models(json.loads(message_data['data']))
        send_result_event(results)


def on_error(ws, error):
    print(f"WebSocket error: {error}")

def on_close(ws, close_status_code, close_msg):
    print("WebSocket closed")

def on_open(ws):
    print("WebSocket connection opened")

def run():
    ws_url = f"wss://ws-{PUSHER_CLUSTER}.pusher.com/app/{PUSHER_KEY}?protocol=7&client=python&version=0.1"
    ws = websocket.WebSocketApp(
        ws_url,
        on_open=on_open,
        on_message=on_message,
        on_error=on_error,
        on_close=on_close,
    )
    ws.run_forever()

if __name__ == "__main__":
    threading.Thread(target=run).start()
