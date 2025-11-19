import requests
import sys
GITHUB_TOKEN = sys.argv[1]
PATH = sys.argv[2]
PR_NUMBER = sys.argv[3]
ALLOWED_USERS = sys.argv[4]
HEADERS = {
    "Authorization": f"token {GITHUB_TOKEN}",
    "Accept": "application/vnd.github.v3+json"
}
url =f"https://api.github.com/repos/{PATH}/issues/{PR_NUMBER}/comments"
response = requests.get(url, headers=HEADERS)
response.raise_for_status()
payload = response.json()
allowed = False
for item in reversed(payload):
    if item["body"].lower() == "bot-approve-pr":
        if item["user"]["login"] in ALLOWED_USERS:
            allowed = True
            break
assert allowed