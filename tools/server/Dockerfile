FROM python:3.11-slim
WORKDIR /app
RUN pip install --no-cache-dir flask pymysql bcrypt
COPY registration_api.py .
EXPOSE 8080
CMD ["python3", "registration_api.py", "--port", "8080"]
