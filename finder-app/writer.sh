#!/bin/bash

# Check if any of the parameters were not specified
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Error: Both arguments were not specified";
    echo "Usage: $0 <FILESDIR> <SEARCHSTR>"
    exit 1
elif [ -z "$1" ]; then
    echo "Error: writefile is missing"
    exit 1
elif [ -z "$2" ]; then
    echo "Error: writestr is missing"
    exit 1
fi

WRITEFILE=$1
WRITESTR=$2

# Extract directory from the full path
DIRPATH=$(dirname "$WRITEFILE")

# Create directory if it doesn't exist
if [ ! -d "$DIRPATH" ]; then
    echo "Directory '$DIRPATH' does not exist. Creating a new one"
    mkdir -p "$DIRPATH" 2>/dev/null
    if [ $? -ne 0 ]; then
        echo "Error: Could not create directory '$DIRPATH'."
        exit 1
    fi
fi

# Create a file and write string into it
echo "$WRITESTR" > "$WRITEFILE"
if [ $? -ne 0 ]; then
    echo "Error: Could not create/write to file '$WRITEFILE'."
    exit 1
fi

echo "File '$WRITEFILE' has been created with content: '$WRITESTR'."
