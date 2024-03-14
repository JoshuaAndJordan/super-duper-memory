from typing import Dict, List, Any
import random, string, json
import requests
import time

global_tokens = {}


class DataF:
    def __init__(self, task_id: str, contracts: int):
        self.task_id = task_id
        self.contracts = contracts


def get_token_list(exchange_name):
    url = "http://localhost:3421/trading_pairs/" + exchange_name

    try:
        response = requests.get(
            url, headers={"Content-Type": "application/json"}
        )
        response.raise_for_status()  # Raise an exception for bad responses

        # Decode the JSON response
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def get_all_price_tasks():
    url = "http://localhost:3421/all_price_tasks"

    try:
        response = requests.get(url, headers={"Content-Type": "application/json"})
        response.raise_for_status()  # Raise an exception for bad responses

        # Decode the JSON response
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def get_price_tasks(user_id):
    url = "http://localhost:3421/list_price_tasks/" + user_id

    try:
        response = requests.get(
            url, headers={"Content-Type": "application/json"}
        )
        response.raise_for_status()  # Raise an exception for bad responses

        # Decode the JSON response
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")


def send_pricing_task(obj):
    payload = json.dumps(obj)
    headers = {"Content-Type": "application/json"}
    url = "http://localhost:3421/add_pricing_tasks"
    requests.request("POST", url, headers=headers, data=payload)


def send_stop_pricing_task(obj):
    payload = json.dumps(obj)
    headers = {"Content-Type": "application/json"}
    url = "http://localhost:3421/stop_price_tasks"
    response = requests.request("POST", url, headers=headers, data=payload)
    return response.json()


def get_all_tokens():
    global global_tokens
    global_tokens = {
        "kucoin": get_token_list("kucoin"),
        "binance": get_token_list("binance"),
        "okex": get_token_list("okex"),
    }
    if None in global_tokens.values():
        raise ValueError("None found in global_tokens")


def generate_random_string(length):
    letters = string.ascii_lowercase
    return "".join(random.choice(letters) for i in range(length))


def generate_random_number(start=5, stop=25):
    return random.randint(start, stop)


def generate_new_pricing_object_contract():
    random_number = generate_random_number(0, 3)
    keys = list(global_tokens.keys())
    exchange = keys[random_number % len(keys)]
    trade_type = random.sample(["futures", "spot"], 1)[0]
    action_type = random.sample(["percentage", "intervals"], 1)[0]
    symbols = []

    while len(symbols) == 0:
        tokens = random.sample(global_tokens[exchange], generate_random_number(3, 10))
        symbols = [obj["name"] for obj in tokens if obj["type"] == trade_type]

    obj = {
        "symbols": symbols,
        "trade": trade_type,
        "exchange": exchange,
    }

    if action_type == "intervals":
        obj["intervals"] = generate_random_number(10, 60)
        obj["duration"] = "seconds"
    else:
        obj["percentage"] = random.uniform(0, 2.0)
        obj["direction"] = random.sample(["up", "down"], 1)[0]

    return obj

def get_price_for(exchange_name, symbol, trade_type):
    url = f"http://localhost:3421/latest_price/{exchange_name}/{trade_type}/{symbol}"

    try:
        response = requests.get(
            url, headers={"Content-Type": "application/json"}
        )
        response.raise_for_status()  # Raise an exception for bad responses

        # Decode the JSON response
        return response.json()
    except requests.exceptions.RequestException as e:
        print(f"Error: {e}")

def generate_add_pricing_list_object() -> Dict[str, Any]:
    obj = {
        "task_id": generate_random_string(generate_random_number()),
        "user_id": generate_random_string(15),
        "contracts": [
            generate_new_pricing_object_contract()
            for i in range(generate_random_number(1, 5))
        ],
    }
    return obj


def stop_task(user_id, task_list):
    obj = {"user_id": user_id, "task_list": [task.task_id for task in task_list]}
    result = send_stop_pricing_task(obj)
    assert result is not None
    assert type(result) is list
    assert len(result) == len(task_list)

def assert_correct_task(data, expected_length):
    assert data is not None
    assert type(data) is list
    assert len(data) == expected_length


def check_user_task_matches(tasks):
    print("Sleeping for 30 seconds...")
    time.sleep(15)  # sleep for 30 seconds

    total_tasks = 0
    task_items = tasks.items()

    for _, task_ids in task_items:
        for task in task_ids:
            total_tasks += task.contracts
    data = get_all_price_tasks()
    assert_correct_task(data, total_tasks)

    counter = 0
    total_items = len(task_items)
    for user_id, task_ids in task_items:
        counter += 1
        total_tasks = 0
        for task in task_ids:
            total_tasks += task.contracts

        data = get_price_tasks(user_id)
        assert_correct_task(data, total_tasks)
        time.sleep(1)

        stop_task(user_id, task_ids)

        data = get_price_tasks(user_id)
        assert_correct_task(data, 0)
        print(f'Remaining {total_items - counter} to test')

    print('Getting all price tasks...')
    data = get_all_price_tasks()
    assert_correct_task(data, 0)


def test_getting_price():
	for exchange_name, tokens in global_tokens.items():
		for token in tokens:
			data = get_price_for(exchange_name, token["name"], token["type"])
			assert data is not None
			assert type(data) is dict
			print(f"{data['name']} -> {data['price']} -> {data['type']} ({exchange_name})")


def main():
    user_tasks: Dict[str, List[DataF]] = {}
    total_tasks = 10
    counter = 0
    for _ in range(total_tasks):
        task = generate_add_pricing_list_object()
        user_id = task["user_id"]
        if user_id not in user_tasks:
            user_tasks[user_id] = []
        user_tasks[user_id].append(DataF(task["task_id"], len(task["contracts"])))
        send_pricing_task(task)
        counter += 1
        print(f'Sent {counter} tasks to server')

    check_user_task_matches(user_tasks)
    test_getting_price()

if __name__ == "__main__":
    get_all_tokens()
    main()
