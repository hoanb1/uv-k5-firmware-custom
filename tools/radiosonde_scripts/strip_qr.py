import re
with open('app/radiosonde.c', 'r') as f:
    code = f.read()

# Remove miniqr_encode extern
code = re.sub(r'extern void miniqr_encode.*?;', '', code)

# Remove Sonde_DrawQRCode function
code = re.sub(r'static void Sonde_DrawQRCode\(const RS41_Data_t \*d\) \{.*?\n\}\n', '', code, flags=re.DOTALL)

# Remove the call to Sonde_DrawQRCode
code = re.sub(r'Sonde_DrawQRCode\(d\);', '', code)

with open('app/radiosonde.c', 'w') as f:
    f.write(code)
