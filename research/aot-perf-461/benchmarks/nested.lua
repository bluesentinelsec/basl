local sum = 0
for i = 0, 999 do
    for j = 0, 999 do
        sum = (sum + i + j) & 0x7FFFFFFF
    end
end
