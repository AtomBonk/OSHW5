import sys

with open(sys.argv[1]) as f1, open(sys.argv[2]) as f2:
    lines1 = [line.strip() for line in f1 if line.strip()]
    lines2 = [line.strip() for line in f2 if line.strip()]

if lines1 == lines2:
    print("Tests Passed - Output PCC_TOTAL matches!")
    sys.exit(0)
else:
    print("Files differ!")
    for l1, l2 in zip(lines1, lines2):
        if l1 != l2:
            print(f"- {l1}\n+ {l2}")
    if len(lines1) != len(lines2):
        print(f"File lengths differ: {len(lines1)} vs {len(lines2)}")
    sys.exit(1)
