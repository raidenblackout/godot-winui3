import os
import glob

replacements = {
    "winui3": "windows_embed",
    "WinUI3": "WindowsEmbed",
    "WINUI3": "WINDOWS_EMBED"
}

files_to_check = []
for ext in ["*.cpp", "*.h", "*.md", "SCsub", "detect.py"]:
    files_to_check.extend(glob.glob(f"platform/windows/**/{ext}", recursive=True))

files_to_check.append("README.md")
files_to_check.append("SConstruct")

for file_path in files_to_check:
    if not os.path.isfile(file_path):
        continue
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    new_content = content
    for old, new in replacements.items():
        new_content = new_content.replace(old, new)
        
    if new_content != content:
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print(f"Updated {file_path}")

