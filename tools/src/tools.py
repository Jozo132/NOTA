
import sys
import os


ENV_FILE = ".temp"

# Load ENV_FILE as a dictionary
def load_env():
    env = {}
    try:
        # Create ENV_FILE file if it doesn't exist
        with open(ENV_FILE, "a") as f:
            pass
        with open(ENV_FILE, "r") as f:
            for line in f:
                line = line.strip()
                if line != "":
                    if line[0] != "#":
                        key, value = line.split("=")
                        key = key.strip()
                        value = value.strip()
                        if key != "": env[key] = value.strip()
    except:
        pass
    return env
env = load_env()


# Load env value
def env_read(key):
    return env.get(key, "")

# Save env value
def env_write(key, value):
    env[key] = value
    with open(ENV_FILE, "w") as f:
        for key, value in env.items():
            f.write(key + " = " + value + "\n")



# Load CLI arguments
SUPPORTED_ARGUMENTS = {
    "TEMP_OTA_FILE": [ "-f", "--file" ],
    "TEMP_OTA_IP": [ "-i", "--ip" ],
    "TEMP_OTA_PORT": [ "-p", "--port" ],
    "TEMP_OTA_AUTH": [ "-a", "--auth" ]
}

def load_args():
    args = {}
    for key, values in SUPPORTED_ARGUMENTS.items():
        for value in values:
            if value in sys.argv:
                index = sys.argv.index(value)
                if index + 1 < len(sys.argv):
                    args[key] = sys.argv[index + 1]
    return args
args = load_args()


# Update env with new args if they were provided
for key, value in args.items():
    env_write(key, value)

