"""DAODroid's PC tool in a window: pick your disc image and a folder, press Make, wait, and copy
the game package it makes to your phone. The same steps as daodroid_tool.py.

    python scripts/daodroid_tool_gui.py
"""
import os
import queue
import sys
import threading
import tkinter as tk
from pathlib import Path
from tkinter import filedialog, messagebox, ttk

sys.path.insert(0, str(Path(__file__).resolve().parent))
import daodroid_tool  # noqa: E402
from installer import toolchain  # noqa: E402  (importable once daodroid_tool set the path)
from installer.util import Cancelled, InstallError, Reporter  # noqa: E402

RED = "#A80F0F"


class Window:
    def __init__(self, root):
        self.root = root
        root.title("DAODroid — make a game package")
        root.geometry("760x560")
        root.configure(bg="white")
        self.events = queue.Queue()
        self.worker = None
        self.reporter = None

        style = ttk.Style()
        style.configure("TLabel", background="white")
        style.configure("Title.TLabel", background="white", foreground=RED, font=("Segoe UI", 16, "bold"))
        style.configure("TCheckbutton", background="white")
        style.configure("TFrame", background="white")

        f = ttk.Frame(root, padding=16)
        f.pack(fill="both", expand=True)
        ttk.Label(f, text="DAODroid", style="Title.TLabel").grid(row=0, column=0, columnspan=3, sticky="w")
        ttk.Label(f, text="Your own Dragon Age: Origins Xbox 360 disc image in, a game package for the "
                          "DAODroid launcher out.", wraplength=700).grid(row=1, column=0, columnspan=3, sticky="w", pady=(2, 12))

        self.iso = tk.StringVar()
        self.out = tk.StringVar(value=str(Path.home() / "Desktop"))
        ttk.Label(f, text="Disc image (.iso)").grid(row=2, column=0, sticky="w")
        ttk.Entry(f, textvariable=self.iso, width=70).grid(row=2, column=1, sticky="we", padx=6)
        ttk.Button(f, text="Browse…", command=self.pick_iso).grid(row=2, column=2)
        ttk.Label(f, text="Save the package to").grid(row=3, column=0, sticky="w", pady=6)
        ttk.Entry(f, textvariable=self.out, width=70).grid(row=3, column=1, sticky="we", padx=6)
        ttk.Button(f, text="Browse…", command=self.pick_out).grid(row=3, column=2)

        self.license = tk.BooleanVar(value=False)
        if sys.platform == "win32" and not toolchain.winsysroot_ready():
            ttk.Checkbutton(f, variable=self.license,
                            text="I accept Microsoft's license for the Windows SDK headers the recompiler is built with").grid(
                row=4, column=0, columnspan=3, sticky="w", pady=4)

        buttons = ttk.Frame(f)
        buttons.grid(row=5, column=0, columnspan=3, sticky="we", pady=(8, 8))
        self.make = tk.Button(buttons, text="Make the package", bg=RED, fg="white", activebackground="#7E0000",
                              activeforeground="white", relief="flat", padx=16, pady=6, command=self.start)
        self.make.pack(side="left")
        self.cancel = ttk.Button(buttons, text="Cancel", command=self.stop, state="disabled")
        self.cancel.pack(side="left", padx=8)

        self.status = ttk.Label(f, text="", wraplength=700)
        self.status.grid(row=6, column=0, columnspan=3, sticky="w")
        self.bar = ttk.Progressbar(f, mode="determinate", maximum=1000)
        self.bar.grid(row=7, column=0, columnspan=3, sticky="we", pady=6)
        self.log = tk.Text(f, height=16, wrap="word", font=("Consolas", 9), relief="solid", borderwidth=1)
        self.log.grid(row=8, column=0, columnspan=3, sticky="nsew")
        ttk.Label(f, text="The package is your own copy of the game: don't share it.").grid(
            row=9, column=0, columnspan=3, sticky="w", pady=(8, 0))
        f.columnconfigure(1, weight=1)
        f.rowconfigure(8, weight=1)
        root.after(100, self.pump)

    def pick_iso(self):
        path = filedialog.askopenfilename(filetypes=[("Disc images", "*.iso"), ("All files", "*.*")])
        if path:
            self.iso.set(path)

    def pick_out(self):
        path = filedialog.askdirectory()
        if path:
            self.out.set(path)

    def start(self):
        if not self.iso.get() or not Path(self.iso.get()).is_file():
            messagebox.showerror("DAODroid", "Choose your disc image first.")
            return
        argv = ["--iso", self.iso.get(), "--out", self.out.get()]
        if self.license.get():
            argv.append("--accept-msvc-license")
        self.reporter = Reporter(log=lambda t: self.events.put(("log", t)),
                                 status=lambda t: self.events.put(("status", t)),
                                 progress=lambda p: self.events.put(("progress", p)))
        self.make.configure(state="disabled")
        self.cancel.configure(state="normal")
        self.log.delete("1.0", "end")
        self.worker = threading.Thread(target=self.run, args=(argv,), daemon=True)
        self.worker.start()

    def run(self, argv):
        try:
            package = daodroid_tool.main(argv, self.reporter)
            self.events.put(("done", str(package)))
        except Cancelled:
            self.events.put(("failed", "Cancelled."))
        except InstallError as e:
            self.events.put(("failed", str(e)))
        except Exception as e:  # noqa: BLE001  (shown to the player, with the log above)
            self.events.put(("failed", f"{type(e).__name__}: {e}"))

    def stop(self):
        if self.reporter:
            self.reporter.cancel_event.set()

    def pump(self):
        try:
            while True:
                kind, value = self.events.get_nowait()
                if kind == "log":
                    self.log.insert("end", value + "\n")
                    self.log.see("end")
                elif kind == "status":
                    self.status.configure(text=value.strip())
                    self.log.insert("end", "\n" + value.strip() + "\n")
                    self.log.see("end")
                elif kind == "progress":
                    if value is None:
                        self.bar.configure(mode="indeterminate")
                        self.bar.start(15)
                    else:
                        self.bar.stop()
                        self.bar.configure(mode="determinate", value=int(value * 1000))
                elif kind in ("done", "failed"):
                    self.bar.stop()
                    self.make.configure(state="normal")
                    self.cancel.configure(state="disabled")
                    if kind == "done":
                        self.bar.configure(mode="determinate", value=1000)
                        messagebox.showinfo("DAODroid", f"Done:\n{value}\n\nCopy it to your phone and add it in "
                                                        "DAODroid (+). It's your own copy of the game: don't share it.")
                        if sys.platform == "win32":
                            os.startfile(Path(value).parent)  # noqa: S606  (opens the folder in Explorer)
                    else:
                        messagebox.showerror("DAODroid", value)
        except queue.Empty:
            pass
        self.root.after(100, self.pump)


def main():
    root = tk.Tk()
    Window(root)
    root.mainloop()


if __name__ == "__main__":
    main()
