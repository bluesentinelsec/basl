local x = 0
for i = 0, 9999999 do
    x = x ~ (i & 0xFF)
    x = x | (i >> 3)
    x = x & 0x7FFFFFFF
end
