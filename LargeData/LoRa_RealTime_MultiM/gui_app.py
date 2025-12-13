import threading
import tkinter as tk
from tkinter import ttk, filedialog, messagebox
from pathlib import Path

from lora_transceiver import LoRaSerialSession


class App(tk.Tk):
    def __init__(self):
        super().__init__()
        self.title("LoRa File TX/RX (One-Port)")
        self.geometry("900x650")

        self.port_var = tk.StringVar(value="COM9")
        self.baud_var = tk.IntVar(value=115200)
        self.out_dir_var = tk.StringVar(value="received_files")

        self.jpeg_quality_var = tk.IntVar(value=85)
        self.mp3_bitrate_var = tk.StringVar(value="64k")
        self.chunk_size_var = tk.IntVar(value=40000)
        self.chunk_timeout_var = tk.DoubleVar(value=300.0)

        self.file_path_var = tk.StringVar()

        self.sess = None

        self._build_ui()

    def _build_ui(self):
        # Root uses PACK for one main container only
        frm = ttk.Frame(self)
        frm.pack(fill=tk.BOTH, expand=True, padx=10, pady=10)

        # ---------------- Connection ----------------
        conn = ttk.LabelFrame(frm, text="Connection (ONE COM owner)")
        conn.grid(row=0, column=0, sticky="we")
        frm.columnconfigure(0, weight=1)

        ttk.Label(conn, text="Serial Port").grid(row=0, column=0, sticky="w")
        ttk.Entry(conn, textvariable=self.port_var, width=12).grid(row=0, column=1, sticky="w", padx=5)

        ttk.Label(conn, text="Baud").grid(row=0, column=2, sticky="w")
        ttk.Entry(conn, textvariable=self.baud_var, width=10).grid(row=0, column=3, sticky="w", padx=5)

        ttk.Label(conn, text="Out Dir").grid(row=0, column=4, sticky="w")
        ttk.Entry(conn, textvariable=self.out_dir_var, width=30).grid(row=0, column=5, sticky="we", padx=5)
        ttk.Button(conn, text="Browse", command=self.choose_dir).grid(row=0, column=6, sticky="w")

        ttk.Button(conn, text="Connect", command=self.connect).grid(row=1, column=5, sticky="e", pady=5)
        ttk.Button(conn, text="Disconnect", command=self.disconnect).grid(row=1, column=6, sticky="w", pady=5)

        conn.columnconfigure(5, weight=1)

        # ---------------- TX controls ----------------
        tx = ttk.LabelFrame(frm, text="Transmit")
        tx.grid(row=1, column=0, sticky="we", pady=10)
        tx.columnconfigure(1, weight=1)

        ttk.Label(tx, text="JPEG Quality").grid(row=0, column=0, sticky="w")
        ttk.Entry(tx, textvariable=self.jpeg_quality_var, width=6).grid(row=0, column=1, sticky="w", padx=5)

        ttk.Label(tx, text="MP3 Bitrate").grid(row=0, column=2, sticky="w")
        ttk.Entry(tx, textvariable=self.mp3_bitrate_var, width=8).grid(row=0, column=3, sticky="w", padx=5)

        ttk.Label(tx, text="Chunk Size (b64 chars)").grid(row=0, column=4, sticky="w")
        ttk.Entry(tx, textvariable=self.chunk_size_var, width=10).grid(row=0, column=5, sticky="w", padx=5)

        ttk.Label(tx, text="Chunk Timeout (s)").grid(row=0, column=6, sticky="w")
        ttk.Entry(tx, textvariable=self.chunk_timeout_var, width=10).grid(row=0, column=7, sticky="w", padx=5)

        ttk.Label(tx, text="File to Send").grid(row=1, column=0, sticky="w")
        ttk.Entry(tx, textvariable=self.file_path_var, width=60).grid(
            row=1, column=1, columnspan=6, sticky="we", padx=5
        )
        ttk.Button(tx, text="Browse", command=self.choose_file).grid(row=1, column=7, sticky="w")

        ttk.Label(tx, text="Or type text").grid(row=2, column=0, sticky="nw")

        # IMPORTANT: parent must be tx (NOT self)
        self.text_input = tk.Text(tx, height=6)
        self.text_input.grid(row=2, column=1, columnspan=7, sticky="we", padx=5, pady=5)

        ttk.Button(tx, text="Send File", command=self.on_send_file).grid(row=3, column=6, sticky="e")
        ttk.Button(tx, text="Send Text", command=self.on_send_text).grid(row=3, column=7, sticky="w")

        # ---------------- Log ----------------
        lg = ttk.LabelFrame(frm, text="Log")
        lg.grid(row=2, column=0, sticky="nsew")
        frm.rowconfigure(2, weight=1)
        lg.rowconfigure(0, weight=1)
        lg.columnconfigure(0, weight=1)

        # IMPORTANT: parent must be lg (NOT self)
        self.log = tk.Text(lg, height=18)
        self.log.grid(row=0, column=0, sticky="nsew", padx=5, pady=5)

        self._log("[INFO] Ready. Click Connect to start listening.\n")

    def _log(self, s: str):
        self.log.insert(tk.END, s)
        self.log.see(tk.END)

    def choose_dir(self):
        d = filedialog.askdirectory()
        if d:
            self.out_dir_var.set(d)

    def choose_file(self):
        p = filedialog.askopenfilename()
        if p:
            self.file_path_var.set(p)

    def connect(self):
        if self.sess is not None:
            messagebox.showinfo("Info", "Already connected.")
            return

        port = self.port_var.get().strip()
        baud = int(self.baud_var.get())
        out_dir = Path(self.out_dir_var.get())

        try:
            self.sess = LoRaSerialSession(port, baud, out_dir=out_dir, quiet=False)
            self.sess.open()
            self._log(f"[INFO] Connected to {port} @ {baud}. Listening started.\n")
        except Exception as e:
            self.sess = None
            messagebox.showerror("Connect failed", str(e))

    def disconnect(self):
        if self.sess is None:
            return
        try:
            self.sess.close()
        finally:
            self.sess = None
            self._log("[INFO] Disconnected.\n")

    def on_send_file(self):
        if self.sess is None:
            messagebox.showerror("Not connected", "Connect first.")
            return

        path = self.file_path_var.get().strip()
        if not path:
            messagebox.showerror("Error", "Choose a file first.")
            return

        def worker():
            try:
                ok = self.sess.send_file(
                    Path(path),
                    chunk_size_chars=int(self.chunk_size_var.get()),
                    jpeg_quality=int(self.jpeg_quality_var.get()),
                    mp3_bitrate=str(self.mp3_bitrate_var.get()),
                    chunk_timeout_s=float(self.chunk_timeout_var.get()),
                )
                self._log(f"[RESULT] Send file: {'OK' if ok else 'FAILED'}\n")
            except Exception as e:
                messagebox.showerror("Send failed", str(e))

        threading.Thread(target=worker, daemon=True).start()

    def on_send_text(self):
        if self.sess is None:
            messagebox.showerror("Not connected", "Connect first.")
            return

        text = self.text_input.get("1.0", tk.END).strip()
        if not text:
            messagebox.showerror("Error", "Type some text.")
            return

        def worker():
            try:
                ok = self.sess.send_text_as_file(
                    text,
                    chunk_size_chars=int(self.chunk_size_var.get()),
                    jpeg_quality=int(self.jpeg_quality_var.get()),
                    mp3_bitrate=str(self.mp3_bitrate_var.get()),
                    chunk_timeout_s=float(self.chunk_timeout_var.get()),
                )
                self._log(f"[RESULT] Send text: {'OK' if ok else 'FAILED'}\n")
            except Exception as e:
                messagebox.showerror("Send failed", str(e))

        threading.Thread(target=worker, daemon=True).start()


if __name__ == "__main__":
    App().mainloop()
