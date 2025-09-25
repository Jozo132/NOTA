####### nota-gui.py ####### 
# This is an OTA GUI tool for simplifying the OTA process.
# The main upload command tool is nota.js, which we run with Node.JS
# This tool is a simple GUI for that tool.

# This tool is written in Python, using the Tkinter library for the GUI.
# The GUI is simple, with a few buttons and text fields for the user to interact with.
# It has the following interface:
# - A text field for the user to enter the path to the firmware file (.bin or .elf)  (can be provided beforehand as a command line argument)
# - A button to browse for the firmware file
# - A text field for the user to enter the IP address of the target device 
# - A text field for the user to enter the port number of the target device
# - A password field for the user to enter the password of the target device
# - A button to start the OTA process
# - A text field to display the output of the OTA process from the command line tool

###### nota.js ######
# This is the main OTA tool, written in Node.JS
# It is a command line tool that takes the following arguments:
# -  [-f] or [--file] : the path to the firmware file (.bin or .elf)

#import everything from local "env.py" module 
from src.tools import *

# Import the Tkinter library
import tkinter as tk
from tkinter import filedialog
from tkinter import messagebox

# Import the subprocess module to run the OTA tool
from subprocess import *

# Get parent folder name from current path
TITLE = os.path.basename(os.getcwd())
script_dir = os.path.dirname(os.path.abspath(__file__))


ip = env_read("TEMP_OTA_IP")
port = env_read("TEMP_OTA_PORT")
auth = env_read("TEMP_OTA_AUTH")
file_path = env_read("TEMP_OTA_FILE")
file_path = file_path.replace("\\", "/")
file = file_path.split("/")[-1]


# Create the main window
root = tk.Tk()
root.title("OTA GUI - by J.Vovk")
root.resizable(False, False)

# Create the title label
title = tk.Label(root, text=TITLE, font=("Helvetica", 16))
title.grid(row=0, column=0, columnspan=4, padx=10, pady=10)

# Create the labels
label1 = tk.Label(root, text="Firmware file:")
label1.grid(row=1, column=0)
label2 = tk.Label(root, text="IP address:")
label2.grid(row=2, column=0)
label3 = tk.Label(root, text="Port:")
label3.grid(row=3, column=0)
label4 = tk.Label(root, text="Password:")
label4.grid(row=4, column=0)

# Create the text fields
entry1 = tk.Entry(root, width=30)
entry1.grid(row=1, column=1)
entry1.insert(0, file)
entry2 = tk.Entry(root, width=30)
entry2.grid(row=2, column=1)
entry2.insert(0, ip)
entry3 = tk.Entry(root, width=30)
entry3.grid(row=3, column=1)
entry3.insert(0, port)
entry4 = tk.Entry(root, show="*", width=30)
entry4.grid(row=4, column=1)
entry4.insert(0, auth)

# Create the browse button, starting from the current directory, hide prefix path and only show the location of the file from current directory
def browse():
    global file_path
    global file

    # Allow only bin and elf files
    new_file = filedialog.askopenfilename(initialdir = "./", title = "Select file", filetypes = (("bin files", "*.bin"), ("elf files", "*.elf")))
    if new_file == "": return
    new_file = new_file.replace("\\", "/")
    file_path = new_file
    entry1.delete(0, tk.END)
    current_dir = os.getcwd()
    # Remove the matching prefix path
    file = file_path.replace(current_dir, ".")

browse_button = tk.Button(root, text="Browse", command=browse, width=10)
browse_button.grid(row=1, column=3, padx=10, pady=3)


def PRINT_STDOUT(msg):
    text.config(state=tk.NORMAL)
    #append the text to the text field
    text.insert(tk.END, msg)
    #scroll to the end of the text field
    text.see(tk.END)
    #update the display
    text.update()
    text.config(state=tk.DISABLED)


# Create the usb_upload button (st-link)
def usb_upload():
    if (not file_path):
        PRINT_STDOUT("Error: Please select a firmware file\n")
        return
    # Run st-flash write command with hardware reset
    # process = Popen(["st-flash", "write", file_path, "0x08000000"], stdout=PIPE, stderr=STDOUT, universal_newlines=True) # for st-link without reset
    process = Popen(["st-flash", "--reset", "write", file_path, "0x08000000"], stdout=PIPE, stderr=STDOUT, universal_newlines=True) # for st-link with reset
    while True:
        output = process.stdout.readline()
        if output == '' and process.poll() is not None:
            break
        if output:
            PRINT_STDOUT(output)

usb_button = tk.Button(root, text="USB Upload", command=usb_upload, width=10)
usb_button.grid(row=3, column=3, padx=10, pady=3)

# Create the ota_upload button
def ota_upload(force=False):
    ip = entry2.get()
    port = entry3.get()
    auth = entry4.get()

    if (not file_path):
        PRINT_STDOUT("Error: Please select a firmware file\n")
        return
    if (not ip):
        PRINT_STDOUT("Error: Please enter the IP address\n")
        return
    if (not port):
        PRINT_STDOUT("Error: Please enter the port\n")
        return
    
    env_write("TEMP_OTA_FILE", file_path)
    env_write("TEMP_OTA_IP", ip)
    env_write("TEMP_OTA_PORT", port)
    env_write("TEMP_OTA_AUTH", auth)
    # nota.js location: ./tools/nota/nota.js

    # Run the OTA tool with live stdout output

    command_pipe = ["node", f"{script_dir}/nota.js"]

    command_pipe.append("-f"); command_pipe.append(file_path)
    command_pipe.append("-i"); command_pipe.append(ip)
    command_pipe.append("-p"); command_pipe.append(port)
    command_pipe.append("-n"); command_pipe.append(TITLE)
    if force:
        command_pipe.append("--force")

    if auth and auth != "":
        command_pipe.append("-a"); command_pipe.append(auth)

    print("Running command: " + " ".join(command_pipe) + "\n")

    process = Popen(command_pipe, stdout=PIPE, stderr=STDOUT, universal_newlines=True)

    while True:
        output = process.stdout.read(1)
        if output == '' and process.poll() is not None:
            break
        if output:
            PRINT_STDOUT(output)

ota_button = tk.Button(root, text="OTA Upload", command=ota_upload, width=10)
ota_button.grid(row=4, column=3, padx=10)
ota_force_button = tk.Button(root, text="OTA Force", command=lambda: ota_upload(True), width=10)
ota_force_button.grid(row=5, column=3, padx=10, pady=3)

# Create the read-only console text field for the output
# Margin 10, wrap text
# height 10, width 80, font Consolas, size 6
# row 5, column 0, columnspan 4
text = tk.Text(root, wrap=tk.WORD, height=20, width=140, font=("Consolas", 8))
text.grid(row=6, column=0, columnspan=4, padx=10, pady=10)
# Make the text field read-only
text.config(state=tk.DISABLED)



# Position the window in the center of the screen
root.update_idletasks()
width = root.winfo_width()
height = root.winfo_height()
x = (root.winfo_screenwidth() // 2) - (width // 2)
y = (root.winfo_screenheight() // 2) - (height // 2)
root.geometry('{}x{}+{}+{}'.format(width, height, x, y))


root.after(1, lambda: root.focus_force())

# Start the main loop
root.mainloop()
