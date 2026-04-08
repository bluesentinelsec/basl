local sum = 0
for i = 0, 99999999 do
    sum = sum + i * 3 - math.floor(i / 2)
end
