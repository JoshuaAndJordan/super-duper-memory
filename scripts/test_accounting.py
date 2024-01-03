from typing import Dict, List
import random, string, json
import requests
import time

def generate_random_string(length):
    letters = string.ascii_lowercase
    return "".join(random.choice(letters) for i in range(length))


def generate_random_number(start=5, stop=25):
    return random.randint(start, stop)


def post_new_account_monitoring(obj):
  payload = json.dumps(obj)
  headers = {"Content-Type": "application/json"}
  url = "http://localhost:3421/add_account_monitoring"
  response = requests.request("POST", url, headers=headers, data=payload)
  return response.json()


def main():
  obj = {
    "exchange": "binance",
    "secret_key": "6LX2dDfxJ6WFszi5IondqBOTkNVRfByb0nV2NM6GYVTIqefahXnIqnT3Cfw0Vwa6",
    "api_key": "CpCu6wDhHLfA7ZTEOqEbREOPuHahJFBzpomVtZJBniqImqyxdrtVgx5tTfRGckdj",
    "pass_phrase": "",
    "trade_type": "spot",
    "task_id": generate_random_string(generate_random_number()),
    "user_id": generate_random_string(generate_random_number())
  }
  data = post_new_account_monitoring(obj)
  print(data)


if __name__ == "__main__":
  main()
