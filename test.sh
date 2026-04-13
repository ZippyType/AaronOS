#!/bin/bash

echo "Starting Deep Surgical Cleanup..."

# Loop until no more files/folders with (1) exist
while find . -name "*(1)*" | grep -q "(1)"; do
    # Get the deepest item to avoid path collapse
    item=$(find . -name "*(1)*" | awk '{ print length, $0 }' | sort -rn | head -n 1 | cut -d" " -f2-)
    
    if [ -z "$item" ]; then break; fi

    # Get the directory part and the filename part separately
    dir=$(dirname "$item")
    base=$(basename "$item")
    
    # Remove (1) from just the filename/foldername itself
    newbase=$(echo "$base" | sed 's/ (1)//g; s/(1)//g')
    newitem="$dir/$newbase"

    if [ "$item" != "$newitem" ]; then
        if [ ! -e "$newitem" ]; then
            echo "Renaming: $item -> $newitem"
            mv "$item" "$newitem"
        else
            if [ -d "$item" ] && [ -d "$newitem" ]; then
                echo "Merging directory contents: $item"
                mv "$item"/* "$newitem/" 2>/dev/null
                rmdir "$item"
            else
                echo "File exists, removing duplicate: $item"
                rm "$item"
            fi
        fi
    fi
done

echo "Deep Cleanup Complete."