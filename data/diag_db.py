import sqlite3
import os

def check_db(db_path):
    print(f"Checking {db_path}...")
    if not os.path.exists(db_path):
        print(f"ERROR: {db_path} not found.")
        return

    try:
        conn = sqlite3.connect(db_path)
        cursor = conn.cursor()
        
        # Check Bible DB
        if "bible" in db_path:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
            tables = [t[0] for t in cursor.fetchall()]
            print(f"  Tables: {tables}")
            
            if 'books' in tables:
                cursor.execute("SELECT id, name, abbreviation FROM books LIMIT 5;")
                print(f"  First 5 books: {cursor.fetchall()}")
            
            if 'verses' in tables:
                cursor.execute("SELECT COUNT(*) FROM verses;")
                print(f"  Total verses: {cursor.fetchone()[0]}")
                cursor.execute("SELECT book_id, chapter, verse, text FROM verses LIMIT 1;")
                print(f"  Sample verse: {cursor.fetchone()}")
        
        # Check Lyrics DB
        elif "lyrics" in db_path:
            cursor.execute("SELECT name FROM sqlite_master WHERE type='table';")
            tables = [t[0] for t in cursor.fetchall()]
            print(f"  Tables: {tables}")
            
            if 'lyrics' in tables:
                cursor.execute("SELECT COUNT(*) FROM lyrics;")
                print(f"  Total songs: {cursor.fetchone()[0]}")
            
            if 'lyrics_fts' in tables:
                print("  FTS table exists.")

        conn.close()
    except Exception as e:
        print(f"  Error: {e}")
    print("-" * 20)

check_db("build_x64/Release/homein-bible.db")
check_db("build_x64/Release/homein-lyrics.db")
