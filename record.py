#!/usr/bin/env python3
import sys
import subprocess
import psycopg2
import datetime
import os
from picamera2 import Picamera2
import cv2 as cv
import time

uid = sys.argv[1]
result = sys.argv[2]

if result != "granted":
	sys.exit(0)

os.makedirs("/home/bianka/videos-access", exist_ok=True)
timestamp = datetime.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
uid_safe = uid.replace(":", "-")
file_path = f"/home/bianka/videos-access/{timestamp}_{uid_safe}_granted.mp4"
'''
subprocess.run([
	"rpicam-vid",
	"-t", "10000",
	"--width", "640",
	"--height", "480",
	"-o", file_path
], check=True)
'''
conn = psycopg2.connect(
	dbname="access_control",
	user="bianka",
	password="raspberry",
	host="localhost"
)
cur = conn.cursor()

# Load face detector
face_cascade = cv.CascadeClassifier(
    "/usr/share/opencv4/haarcascades/haarcascade_frontalface_default.xml"
)

# Open camera
picam2 = Picamera2()
picam2.configure(
	picam2.create_preview_configuration(
		main={"format": "RGB888", "size": (640, 480)}
	)
)
picam2.start()
fourcc = cv.VideoWriter_fourcc(*'avc1')
out = cv.VideoWriter(file_path, fourcc, 20, (640, 480))
start = time.time()

total_faces = 0
while True:
	frame = picam2.capture_array()
	gray = cv.cvtColor(frame, cv.COLOR_RGB2GRAY)
	faces = face_cascade.detectMultiScale(gray, 1.1, 4)
	total_faces = max(total_faces, len(faces))
	for (x, y, w, h) in faces:
        	cv.rectangle(frame, (x, y), (x+w, y+h), (0, 255, 0), 2)
	
	out.write(frame)
	if time.time() - start > 10:
		break
cur.execute(
	"INSERT INTO videos (uid, file_path, duration_seconds, result, faces_count) VALUES (%s, %s, %s, %s, %s)",
	(uid, file_path, 10, "granted", total_faces)
)

conn.commit()
cur.close()
conn.close()
out.release()
print(file_path)
