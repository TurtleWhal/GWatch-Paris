import sys
import csv
import math

GROUP_SIZE = 60 * 10 # 10 minutes

# sys.argv[0] is the script name (e.g., myscript.py)
# sys.argv[1] is the first argument (your filename)
filename = sys.argv[1]

with open(filename, 'r') as f:
    reader = csv.reader(f)
    next(reader)  # Skip header row
 
    initial_time = 0
    groups = [0] * 100
    data = [0] * 100

    for row in reader:
        if initial_time == 0:
            initial_time = int(row[0])

        idx = int(row[0]) // GROUP_SIZE - initial_time // GROUP_SIZE
        print(idx, float(row[3]))
        groups[idx] += 1
        data[idx] += float(row[3])

    for (row, i) in zip(data, range(len(data))):
        if groups[i] > 0:
            print(row / groups[i])