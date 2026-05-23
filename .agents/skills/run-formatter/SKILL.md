---
name: run-formatter
description: Run the local './tools/format.sh' script to format C++, Java, BUILD, and proto files whenever files have been edited, modified, or added in the workspace.
version: 1.0.0
tags:
  - formatting
  - style
  - workflow
---

# Run Formatter Skill

## Goal
Ensure all C++, Java, BUILD, and proto files are formatted correctly using the repository's formatter.

## Instructions
1. Whenever you modify, create, or edit any code files in the repository, you must execute the formatting script `./tools/format.sh`.
2. Run the command `./tools/format.sh` from the workspace root.
3. Ensure that the formatting script executes successfully and reports formatted files.
