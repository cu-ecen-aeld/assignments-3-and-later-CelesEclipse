#!/bin/bash

# Check if any of the parameters were not specified
if [ -z "$1" ] || [ -z "$2" ]; then
    echo "Error: Both arguments were not specified";
    echo "Usage: $0 <FILESDIR> <SEARCHSTR>"
    exit 1
elif [ -z "$1" ]; then
    echo "Error: filesdir is missing"
    exit 1
elif [ -z "$2" ]; then
    echo "Error: searchstr is missing"
    exit 1
fi

# Check if filesdir does not represent a directory on the filesystem
if [ ! -d "$1" ]; then
    echo "Error: '$1' No such file or directory"
    exit 1
fi

FILESDIR=$1
SEARCHSTR=$2

# Count the number of files in $1
NUM_FILES=$(find "$FILESDIR" -type f | wc -l)

# Count the number of matching lines
MATCHSTR=$(grep -r "$SEARCHSTR" "$FILESDIR" 2>/dev/null | wc -l)

echo "The number of files are $NUM_FILES and the number of matching lines are $MATCHSTR"
