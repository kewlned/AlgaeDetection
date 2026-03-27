# 🌊 Pre-emptive Monitoring and Mitigation of Potential Cyanobacteria Blooms

A machine learning-integrated IoT system designed for early detection and correlation analysis of water quality parameters to prevent harmful algal blooms.

### 📦 Technologies

* **Languages:** Python (Pandas, NumPy, Scikit-Learn), C++ (Arduino/ESP32)
* **Hardware:** IoT Water Quality Sensors (pH, Temperature, Turbidity, Nitrate, Blue-Green Algae)
* **Algorithms:** Random Forest Classifier, Data Correlation Analysis
* **Tools:** TabluePlus and Microsoft Excel for data handling, Git, Pandas for initial data cleaning

### 🚀 Features

* **Real-time Data Acquisition:** Continuous monitoring of water parameters via IoT-integrated microcontrollers.
* **Predictive Analytics:** Uses a Machine Learning model to identify the probability of a bloom before it happens.
* **Parameter Correlation:** Analyzes the relationship between environmental factors (like temperature and pH) and Cyanobacteria growth.
* **Automated Mitigation Alerts:** Designed to trigger notifications when parameters reach critical thresholds.

### 📜 The Process

The project started with a critical environmental problem: the unpredictable nature of Cyanobacteria blooms in local water bodies. I wanted to move from reactive treatment to **proactive monitoring**.

First, I designed the **Hardware Layer**, selecting and calibrating sensors to ensure data integrity. I programmed the microcontrollers in **C++** to handle real-time data transmission.

Next, I focused on the **Data Layer**. I collected and cleaned large environmental datasets using **Python**. The challenge was ensuring the sensors were accurate enough for a Machine Learning model to produce reliable predictions. 

Finally, I integrated a **Random Forest model**. I chose this algorithm because it handled the non-linear correlations of water parameters effectively, providing a high accuracy rate in predicting "Bloom" vs. "No Bloom" states.

### 🧠 What I Learned

During this project, I've picked up important skills and a better understanding of complex systems, which improved my logical thinking:

* **Sensor Calibration:** I learned the hard way that data is only as good as the sensor's calibration. This required deep attention to detail.
* **ML Integration:** This project taught me how to bridge the gap between physical hardware signals and digital predictive models.
* **Data Cleaning:** I realized that 70% of engineering in machine learning models is actually preparing the data. I became proficient in using **Pandas** for handling outliers and missing values.

### 🚦 Running the Project

To deploy the system and monitor real-time water quality, follow these operational steps:
**1. Hardware Initialization:** Power up the ESP32/Microcontroller and ensure all submerged sensors (pH, Temp, Turbidity, Blue-Green Algae, Nitrate) are calibrated.
**2. Database Connection:** Launch TablePlus (or your preferred GUI) and connect to the project's SQL database. Ensure the incoming sensor logs are being populated in the water_metrics table.
<img width="2048" height="1448" alt="508354681_1056549762699613_8431366647872853957_n (1)" src="https://github.com/user-attachments/assets/26437297-a73b-454b-87b8-62593fe7a7cd" />

**3. Real-Time Monitoring:** Open the Dashboard (Streamlit/Matplotlib) to visualize the live correlation and prediction trends. The Random Forest model will continuously classify the risk level based on incoming data.
<img width="678" height="471" alt="image" src="https://github.com/user-attachments/assets/2c5ec205-587d-4cee-9fd7-2932693e33f2" />

**4. Mitigation Protocol:** Monitor the Algae Risk Index on the dashboard. IF levels approach the Critical Threshold (e.g., >70% probability), manually  trigger the Ultrasonic Transducer through the control dashboard to inhibit cyanobacteria cell buoyancy and mitigate the bloom.
   
### 📽️ Picture
<img width="1920" height="1164" alt="image" src="https://github.com/user-attachments/assets/015af622-aa28-49f8-bba9-166e7c17b159" />

