import os

import pandas as pd
import numpy as np
import joblib

from dotenv import load_dotenv
load_dotenv()

from sklearn.model_selection import train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.metrics import classification_report, confusion_matrix
from sklearn.svm import SVC
from sklearn.linear_model import LogisticRegression
from sklearn.ensemble import RandomForestClassifier


# data.csv exists with 5 features and a outcome column
df = pd.read_csv("data.csv")  
X = df.drop("outcome", axis=1)
y = df["outcome"]

# Preprocess
scaler = StandardScaler()
X_scaled = scaler.fit_transform(X)

# Split
X_train, X_test, y_train, y_test = train_test_split(X_scaled, y, test_size=0.2, random_state=42)

MODEL_DIR = "models"
os.makedirs(MODEL_DIR, exist_ok=True)

def evaluate_model(name, model, filename):
    model.fit(X_train, y_train)
    y_pred = model.predict(X_test)
    
    print(f"\n====== {name} ======")
    print("Confusion Matrix:")
    print(confusion_matrix(y_test, y_pred))
    print("Classification Report:")
    print(classification_report(y_test, y_pred))

    # Save model
    joblib.dump(model, f"{MODEL_DIR}/{filename}.pkl")
    print(f"✅ Saved {name} model to {MODEL_DIR}/{filename}.pkl")

# SVM
evaluate_model("SVM (RBF)", SVC(kernel='rbf', C=1.0, gamma='scale'), "svm_model")

# Logistic Regression
evaluate_model("Logistic Regression", LogisticRegression(max_iter=1000), "logreg_model")

# Random Forest
evaluate_model("Random Forest", RandomForestClassifier(n_estimators=100, random_state=42), "rf_model")
