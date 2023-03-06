import sys
import json
import requests as req

SECRET_KEY = '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'  # Dummy secret key so don't hack me :D

processes = sys.argv[1:]

question = "What do these processes in Linux do? Answer with at least 300 words: "
question += ', '.join(processes)

request_body = {
    'model': 'gpt-3.5-turbo',
    'messages': [
        {
            'role': 'user',
            'content': question
        }
    ]
}

request_headers = {
    'Authorization': f'Bearer {SECRET_KEY}'
}
res = req.post('https://api.openai.com/v1/chat/completions', json=request_body, headers=request_headers, timeout=30)

data = res.json()
print(data['choices'][0]['message']['content'])