import socket
import cv2
import numpy as np
import struct
import time
import os

UDP_IP = "0.0.0.0"
UDP_PORT = 2222

# OpenCV Backend Force (wichtig!)
os.environ["OPENCV_IO_ENABLE_JASPER"] = "true"
os.environ["OPENCV_IO_ENABLE_OPENEXR"] = "false"

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((UDP_IP, UDP_PORT))
sock.settimeout(1.0)

buf = bytearray()
frame_count = 0
fps_counter = 0
last_fps_time = time.time()

print("🎥 ESP32-CAM DEBUG MODE")
print("Drücke 'q' oder ESC zum Beenden")

while True:
    try:
        data, addr = sock.recvfrom(65535)
        buf.extend(data)
    except socket.timeout:
        if time.time() - last_fps_time > 2.0:
            print(f"⏱️  FPS: {fps_counter} | Buffer: {len(buf)}B")
            fps_counter = 0
            last_fps_time = time.time()
        continue

    # Wir suchen im Buffer nach dem Sync-Muster
    i = 0
    while i + 8 < len(buf): # Brauchen Platz für Längen-Info(4) + Sync(5)
        if (buf[i+4] == 0xFF and buf[i+5] == 0xD8 and 
            buf[i+6] == 0xFF and buf[i+7] == 0xAA and buf[i+8] == 0x55):
            
            header_start = i
            # Lese die erwartete Länge aus den ersten 4 Bytes
            expected_len = struct.unpack('<I', buf[header_start:header_start+4])[0]
            
            # Die Gesamtgröße dieses Datenpakets (Header + JPEG)
            total_packet_size = 12 + expected_len
            
            # Haben wir schon alle Fragmente dieses Frames empfangen?
            if len(buf) - header_start >= total_packet_size:
                # Schneide exakt das JPEG heraus (Header überspringen)
                jpeg_data = buf[header_start + 12 : header_start + total_packet_size]
                
                nparr = np.frombuffer(jpeg_data, np.uint8)
                img = cv2.imdecode(nparr, cv2.IMREAD_COLOR)
                
                if img is not None:
                    fps_counter += 1
                    cv2.imshow("ESP32-CAM", img)
                    key = cv2.waitKey(1) & 0xFF
                    if key == ord('q') or key == 27:
                        sock.close()
                        cv2.destroyAllWindows()
                        exit()
                else:
                    print(" ❌ DECODE FAILED")
                
                # Buffer aufräumen: Lösche alles bis zum Ende dieses Frames
                del buf[:header_start + total_packet_size]
                i = 0 # Buffer hat sich geändert, Suche von vorne starten
                continue
            else:
                # Wir haben den Header, aber das Bild ist noch nicht komplett angekommen.
                # Schleife abbrechen und auf nächste UDP-Pakete warten.
                break 
        i += 1
        
    if len(buf) > 200000:
        print("Buffer Overflow, leere Buffer...")
        buf.clear()

cv2.destroyAllWindows()
sock.close()
print("Stream beendet!")