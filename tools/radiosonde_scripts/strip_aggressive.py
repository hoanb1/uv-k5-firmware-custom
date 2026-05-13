import re

with open('app/qrcodegen.c', 'r') as f:
    code = f.read()

def remove_func(name, code):
    return re.sub(r'(bool|int|void|enum qrcodegen_Mode|struct qrcodegen_Segment|size_t)\s+' + name + r'\s*\([^{]*\)\s*\{.*?\n\}', 
                  '', code, flags=re.DOTALL)

code = remove_func('qrcodegen_encodeText', code)
code = remove_func('qrcodegen_makeNumeric', code)
code = remove_func('qrcodegen_makeAlphanumeric', code)
code = remove_func('qrcodegen_makeEci', code)
code = remove_func('qrcodegen_isNumeric', code)
code = remove_func('qrcodegen_isAlphanumeric', code)
code = remove_func('getPenaltyScore', code)
code = remove_func('finderPenaltyCountPatterns', code)
code = remove_func('finderPenaltyAddHistory', code)

with open('app/qrcodegen.c', 'w') as f:
    f.write(code)
