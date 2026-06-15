FROM python:3.11-slim

WORKDIR /app

# Install dependencies first (layer cached separately from source code)
COPY src/requirements.txt .
RUN pip install --no-cache-dir -r requirements.txt

# Copy application source
COPY src/ ./src/

# Pre-create directories that may not exist in mounted volumes
RUN mkdir -p /app/data/sessions /app/credentials

EXPOSE 8050

CMD ["python", "src/dashboard.py"]
