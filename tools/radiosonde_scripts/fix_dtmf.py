import re

with open('ui/main.c', 'r') as f:
    code = f.read()

code = code.replace('gDTMF_CallState != DTMF_CALL_STATE_NONE', '(0)')
code = code.replace('gDTMF_CallState == DTMF_CALL_STATE_CALL_OUT', '(0)')
code = code.replace('gDTMF_CallState == DTMF_CALL_STATE_RECEIVED', '(0)')
code = code.replace('gDTMF_CallState == DTMF_CALL_STATE_RECEIVED_STAY', '(0)')
code = code.replace('gDTMF_IsTx', '(0)')

with open('ui/main.c', 'w') as f:
    f.write(code)
