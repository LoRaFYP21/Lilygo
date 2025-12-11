import threading
import time
from pathlib import Path
import tkinter as tk
from tkinter import ttk, filedialog, messagebox

from tx_send_file import send_file
from rx_receive_file import MessageReassembler, FileChunkAssembler, handle_full_payload
import serial

# Optional runtime deps for capture/record
try:
    import sounddevice as sd  # pip install sounddevice
    import numpy as np
    from scipy.io.wavfile import write as wav_write  # pip install scipy
except Exception:
    sd = None
    np = None
    wav_write = None

try:
    import cv2  # pip install opencv-python
except Exception:
    cv2 = None

class TxFrame(ttk.Frame):
    def __init__(self, master):
        super().__init__(master)
        self.serial_port_var = tk.StringVar(value="COM9")
        self.jpeg_quality_var = tk.IntVar(value=85)
        self.mp3_bitrate_var = tk.StringVar(value="64k")
        self.file_path_var = tk.StringVar()
        self.text_input = tk.Text(self, height=6)

        ttk.Label(self, text="TX Serial Port").grid(row=0, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.serial_port_var, width=12).grid(row=0, column=1, sticky="w")

        ttk.Label(self, text="JPEG Quality").grid(row=1, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.jpeg_quality_var, width=6).grid(row=1, column=1, sticky="w")

        ttk.Label(self, text="MP3 Bitrate").grid(row=2, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.mp3_bitrate_var, width=8).grid(row=2, column=1, sticky="w")

        # File chooser
        ttk.Label(self, text="File to Send").grid(row=3, column=0, sticky="w")
        file_entry = ttk.Entry(self, textvariable=self.file_path_var, width=50)
        file_entry.grid(row=3, column=1, sticky="we")
        ttk.Button(self, text="Browse", command=self.choose_file).grid(row=3, column=2, sticky="w")

        # Text send area
        ttk.Label(self, text="Or type text to send").grid(row=4, column=0, sticky="w")
        self.text_input.grid(row=4, column=1, columnspan=2, sticky="we")

        # Buttons
        ttk.Button(self, text="Send File", command=self.on_send_file).grid(row=5, column=1, sticky="e")
        ttk.Button(self, text="Send Text", command=self.on_send_text).grid(row=5, column=2, sticky="w")

        # Capture/Record actions
        ttk.Label(self, text="Quick Capture").grid(row=6, column=0, sticky="w")
        ttk.Button(self, text="Record Voice", command=self.on_record_voice).grid(row=6, column=1, sticky="w")
        ttk.Button(self, text="Capture Photo", command=self.on_capture_photo).grid(row=6, column=2, sticky="w")

        self.columnconfigure(1, weight=1)

    def choose_file(self):
        path = filedialog.askopenfilename()
        if path:
            self.file_path_var.set(path)

    def on_send_file(self):
        port = self.serial_port_var.get()
        path = self.file_path_var.get()
        if not path:
            messagebox.showerror("Error", "Please choose a file to send")
            return
        try:
            send_file(serial_port=port, file_path=path, jpeg_quality=self.jpeg_quality_var.get(), mp3_bitrate=self.mp3_bitrate_var.get())
            messagebox.showinfo("Sent", f"Sent '{Path(path).name}' over {port}")
        except Exception as e:
            messagebox.showerror("Send failed", str(e))

    def on_send_text(self):
        port = self.serial_port_var.get()
        text = self.text_input.get("1.0", tk.END).encode("utf-8")
        if not text.strip():
            messagebox.showerror("Error", "Please type some text")
            return
        # Save to a temp file and send
        tmp = Path("_tmp_text_to_send.txt")
        tmp.write_bytes(text)
        try:
            send_file(serial_port=port, file_path=str(tmp))
            messagebox.showinfo("Sent", "Text sent as file")
        finally:
            try:
                tmp.unlink()
            except Exception:
                pass

    def on_record_voice(self):
        if sd is None or wav_write is None or np is None:
            messagebox.showerror("Missing deps", "Audio recording requires 'sounddevice' and 'scipy'. Install them in the venv: pip install sounddevice scipy")
            return
        port = self.serial_port_var.get()
        # Simple modal to ask duration and samplerate
        dur = tk.simpledialog.askinteger("Record Voice", "Duration (seconds)", minvalue=1, maxvalue=60)
        if not dur:
            return
        sr = tk.simpledialog.askinteger("Record Voice", "Sample rate (Hz)", initialvalue=16000, minvalue=8000, maxvalue=48000)
        if not sr:
            return
        try:
            messagebox.showinfo("Recording", f"Recording {dur}s at {sr} Hz...")
            audio = sd.rec(int(dur*sr), samplerate=sr, channels=1, dtype='int16')
            sd.wait()
            tmp_wav = Path("_tmp_record.wav")
            wav_write(tmp_wav, sr, audio)
            # Send; MP3 conversion handled by tx_send_file
            send_file(serial_port=port, file_path=str(tmp_wav))
            messagebox.showinfo("Sent", "Recorded voice sent")
        except Exception as e:
            messagebox.showerror("Record failed", str(e))
        finally:
            try:
                tmp_wav.unlink()
            except Exception:
                pass

    def on_capture_photo(self):
        if cv2 is None:
            messagebox.showerror("Missing deps", "Photo capture requires 'opencv-python'. Install it in the venv: pip install opencv-python")
            return
        port = self.serial_port_var.get()
        cap = cv2.VideoCapture(0)
        if not cap.isOpened():
            messagebox.showerror("Camera error", "Cannot open default camera (index 0)")
            return
        try:
            ret, frame = cap.read()
            if not ret:
                messagebox.showerror("Capture failed", "Could not read frame from camera")
                return
            tmp_jpg = Path("_tmp_capture.jpg")
            cv2.imwrite(str(tmp_jpg), frame)
            send_file(serial_port=port, file_path=str(tmp_jpg))
            messagebox.showinfo("Sent", "Captured photo sent")
        except Exception as e:
            messagebox.showerror("Capture failed", str(e))
        finally:
            cap.release()
            try:
                tmp_jpg.unlink()
            except Exception:
                pass

class RxFrame(ttk.Frame):
    def __init__(self, master):
        super().__init__(master)
        self.serial_port_var = tk.StringVar(value="COM12")
        self.baud_var = tk.IntVar(value=115200)
        self.out_dir_var = tk.StringVar(value="received_files")
        self.log = tk.Text(self, height=15)
        self._stop = threading.Event()
        self._thread = None

        ttk.Label(self, text="RX Serial Port").grid(row=0, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.serial_port_var, width=12).grid(row=0, column=1, sticky="w")

        ttk.Label(self, text="Baud").grid(row=1, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.baud_var, width=8).grid(row=1, column=1, sticky="w")

        ttk.Label(self, text="Out Dir").grid(row=2, column=0, sticky="w")
        ttk.Entry(self, textvariable=self.out_dir_var, width=30).grid(row=2, column=1, sticky="we")
        ttk.Button(self, text="Browse", command=self.choose_dir).grid(row=2, column=2, sticky="w")

        ttk.Button(self, text="Start Listening", command=self.start_listen).grid(row=3, column=1, sticky="e")
        ttk.Button(self, text="Stop", command=self.stop_listen).grid(row=3, column=2, sticky="w")

        self.log.grid(row=4, column=0, columnspan=3, sticky="nsew")
        self.columnconfigure(1, weight=1)
        self.rowconfigure(4, weight=1)

    def choose_dir(self):
        d = filedialog.askdirectory()
        if d:
            self.out_dir_var.set(d)

    def start_listen(self):
        if self._thread and self._thread.is_alive():
            return
        self._stop.clear()
        self._thread = threading.Thread(target=self._listen_loop, daemon=True)
        self._thread.start()

    def stop_listen(self):
        self._stop.set()

    def _listen_loop(self):
        port = self.serial_port_var.get()
        baud = self.baud_var.get()
        out_dir = Path(self.out_dir_var.get())
        reasm = MessageReassembler()
        file_asm = FileChunkAssembler(out_dir)
        try:
            with serial.Serial(port, baud, timeout=1) as ser:
                time.sleep(2.0)
                self._log("[INFO] Listening on %s @ %d...\n" % (port, baud))
                while not self._stop.is_set():
                    line = ser.readline().decode(errors="ignore").strip()
                    if not line:
                        continue
                    self._log(line + "\n")
                    if line.startswith("MSG,"):
                        parts = line.split(",", 5)
                        if len(parts) >= 6:
                            _, src, seq, rssi, d_m, text = parts
                            handle_full_payload(text, file_asm)
                    elif line.startswith("FRAG,"):
                        parts = line.split(",", 7)
                        if len(parts) >= 8:
                            _, src, seq, idx, tot, rssi, d_m, chunk = parts
                            try:
                                seq_i = int(seq); idx_i = int(idx); tot_i = int(tot)
                            except ValueError:
                                continue
                            full = reasm.add_frag(src, seq_i, idx_i, tot_i, chunk)
                            if full is not None:
                                handle_full_payload(full, file_asm)
        except Exception as e:
            self._log(f"[ERROR] {e}\n")

    def _log(self, text: str):
        self.log.insert(tk.END, text)
        self.log.see(tk.END)

class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("LoRa File TX/RX")
        self.geometry("800x600")
        nb = ttk.Notebook(self)
        tx = TxFrame(nb)
        rx = RxFrame(nb)
        nb.add(tx, text="Transmit")
        nb.add(rx, text="Receive")
        nb.pack(fill=tk.BOTH, expand=True)

if __name__ == "__main__":
    App().mainloop()
