import tkinter as tk
from tkinter import ttk
import ctypes
import sys

# Minimal script to test X11 initialization stability
def main():
    print("Testing X11 initialization...")
    
    # Critical order for XInitThreads in some environments
    try:
        libx11 = ctypes.CDLL('libX11.so.6')
        libx11.XInitThreads()
        print("XInitThreads called")
        # libx11.XSynchronize(None, True)
        # print("XSynchronize called")
    except Exception as e:
        print(f"X11 init error: {e}")

    root = tk.Tk()
    root.title("X11 Stability Test")
    
    label = ttk.Label(root, text="If you see this window without a crash, X11 is stable.")
    label.pack(padx=20, pady=20)
    
    btn = ttk.Button(root, text="Quit", command=root.destroy)
    btn.pack(pady=10)
    
    print("Mainloop starting...")
    root.mainloop()
    print("Done.")

if __name__ == "__main__":
    main()
