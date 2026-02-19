import sys
import os
from mypy import api

def main():
    # Set MYPYPATH to only the current directory to avoid searching parents
    os.environ["MYPYPATH"] = "."
    
    args = [
        'gui.py',
        '--ignore-missing-imports',
        '--strict',
        '--explicit-package-bases',
        '--namespace-packages',
        '--no-incremental',
        '--cache-dir=/dev/null',
    ]
    
    print(f"Running mypy with args: {args}")
    # We call api.run which doesn't use the env var by itself, 
    # but mypy internally reads MYPYPATH.
    result = api.run(args)
    
    if result[0]:
        print("Mypy stdout:")
        print(result[0])
    if result[1]:
        print("Mypy stderr:")
        print(result[1])
        
    sys.exit(result[2])

if __name__ == "__main__":
    main()
