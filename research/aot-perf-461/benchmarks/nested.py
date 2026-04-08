sum = 0
for i in range(1000):
    for j in range(1000):
        sum = (sum + i + j) & 0x7FFFFFFF
