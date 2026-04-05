import json
import os
import sys
import yfinance as ticker
import requests
from bs4 import BeautifulSoup
from datetime import datetime

# 데이터 저장 경로 설정
BASE_DIR = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA_DIR = os.path.join(BASE_DIR, 'data')
FILE_PATH = os.path.join(DATA_DIR, 'data.json')

def get_stock_info(symbol, name):
    """Yahoo Finance를 통해 주가 및 지수 정보를 가져옵니다."""
    try:
        stock = ticker.Ticker(symbol)
        data = stock.history(period='2d')
        if len(data) < 2:
            return {"name": name, "price": "N/A", "change": "0.0"}
        
        current_price = data['Close'].iloc[-1]
        prev_price = data['Close'].iloc[-2]
        change_pct = ((current_price - prev_price) / prev_price) * 100
        
        if symbol.endswith('.KS') or symbol.endswith('.KQ'):
            price_str = f"{int(current_price):,}" if current_price >= 100 else f"{current_price:.2f}"
        elif symbol.startswith('^'): # 지수의 경우 소수점 2자리
            price_str = f"{current_price:,.2f}"
        else:
            price_str = f"{current_price:.2f}"
            
        sign = "+" if change_pct >= 0 else ""
        return {
            "name": name,
            "price": price_str,
            "change": f"{sign}{change_pct:.2f}"
        }
    except Exception as e:
        return {"name": name, "price": "Error", "change": "0.0"}

def fetch_real_data():
    # 1. 지수 정보
    indices = [
        get_stock_info("^KS11", "코스피"),
        get_stock_info("^KQ11", "코스닥")
    ]
    
    # 2. 주식 종목 (4개로 제한)
    stock_list = {
        "005930.KS": "삼성전자",
        "000660.KS": "SK하이닉스",
        "085660.KQ": "차바이오텍",
        "AAPL": "애플"
    }
    stocks = [get_stock_info(sym, name) for sym, name in stock_list.items()]
    
    return indices, stocks

def fetch_real_news():
    rss_url = "https://www.yonhapnewstv.co.kr/browse/feed/"
    exclude_keywords = ["다시보기", "클로징", "[날씨]", "[영상]", "날씨", "헤드라인"]
    try:
        response = requests.get(rss_url, timeout=10)
        soup = BeautifulSoup(response.content, 'xml')
        items = soup.find_all('item')
        news_list = []
        for item in items:
            title = item.title.get_text().strip()
            if not any(kw in title for kw in exclude_keywords) and len(title) > 5:
                title = title.replace("[속보]", "").strip()
                news_list.append(title)
            if len(news_list) >= 5: break
        return news_list
    except Exception as e:
        return ["뉴스를 불러올 수 없습니다."]

def generate_json():
    print(f"[{datetime.now()}] 실시간 데이터 수집 중...")
    indices, stocks = fetch_real_data()
    news = fetch_real_news()
    
    data = {
        "indices": indices,
        "stocks": stocks,
        "news": news,
        "last_updated": datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    }

    if not os.path.exists(DATA_DIR):
        os.makedirs(DATA_DIR)

    with open(FILE_PATH, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f"[{datetime.now().strftime('%H:%M:%S')}] data.json 업데이트 성공!")

if __name__ == "__main__":
    generate_json()
