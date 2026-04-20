import sqlite3
import os

db_path = "build_x64/Release/homein-bible.db"

if not os.path.exists(db_path):
    # Try alternate path
    db_path = "data/homein-bible.db"

print(f"Inspecting {db_path}...")
conn = sqlite3.connect(db_path)
cursor = conn.cursor()

# Get table names and types
cursor.execute("SELECT type, name, sql FROM sqlite_master;")
for row in cursor.fetchall():
    print(f"Type: {row[0]}, Name: {row[1]}")
    # print(f"SQL: {row[2]}")

print("\nSample from 'verses' table:")
try:
    cursor.execute("PRAGMA table_info(verses);")
    print(f"Columns: {[c[1] for c in cursor.fetchall()]}")
    cursor.execute("SELECT * FROM verses LIMIT 3;")
    print(cursor.fetchall())
except Exception as e:
    print(f"Error: {e}")

print("\nChecking for virtual tables (FTS):")
cursor.execute("SELECT name FROM sqlite_master WHERE type='table' AND sql LIKE '%VIRTUAL %';")
fts_tables = cursor.fetchall()
print(f"FTS Tables: {fts_tables}")

conn.close()
