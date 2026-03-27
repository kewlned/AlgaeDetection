# Use an official Python runtime as a parent image
FROM python:3.9-slim

# Set the working directory in the container
WORKDIR /app

# Copy the current directory (including Python script) into the container's /app directory
COPY ./machine_learning/python/ /app/

# Install dependencies from requirements.txt
RUN pip install --no-cache-dir -r /app/requirements.txt

# Run the script when the container launches
CMD ["python", "/app/predict_realtime.py"]
