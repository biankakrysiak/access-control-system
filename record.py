#!/usr/bin/env python3
import sys
import subprocess
import psycopg2
import datetime
import os

uid = sys.argv[1]
result = sys.argv[2]

if result != "granted":
    sys.exit(0)

os.makedirs("/home/bianka/videos-access", exist_ok=True)
timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
uid_safe = uid.replace(":", "-")
file_path = f"/home/bianka/videos-access/{timestamp}_{uid_safe}_granted.mp4"

subprocess.run([
    "rpicam-vid",
    "-t", "10000",
    "--width", "640",
    "--height", "480",
    "-o", file_path
], check=True)

conn = psycopg2.connect(
    dbname="access_control",
    user="bianka",
    password="raspberry",
    host="localhost"
)
cur = conn.cursor()

cur.execute(
    "INSERT INTO videos (uid, file_path, duration_seconds, result) VALUES (%s, %s, %s, %s)",
    (uid, file_path, 10, "granted")
)

conn.commit()
cur.close()
conn.close()

print(file_path)
