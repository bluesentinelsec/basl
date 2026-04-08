x = 0
for i in range(10000000):
    x = x ^ (i & 0xFF)
    x = x | (i >> 3)
    x = x & 0x7FFFFFFF
