import os
import glob

replacements = {
    "winui3": "windows_embed",
    "WinUI3": "WindowsEmbed",
    "WINUI3": "WINDOWS_EMBED"
}

project_dir = "platform/windows/WinUI3_Project"

# First, replace contents inside all files in WinUI3_Project
files_to_check = []
for root, dirs, files in os.walk(project_dir):
    for f in files:
        if f.endswith(".cs") or f.endswith(".csproj") or f.endswith(".sln") or f.endswith(".xaml"):
            files_to_check.append(os.path.join(root, f))

for file_path in files_to_check:
    with open(file_path, "r", encoding="utf-8") as f:
        content = f.read()
    
    new_content = content
    for old, new in replacements.items():
        new_content = new_content.replace(old, new)
        
    if new_content != content:
        with open(file_path, "w", encoding="utf-8") as f:
            f.write(new_content)
        print(f"Updated content in {file_path}")

# Now, rename files
for root, dirs, files in os.walk(project_dir, topdown=False):
    for name in files:
        new_name = name
        for old, new in replacements.items():
            new_name = new_name.replace(old, new)
        if new_name != name:
            os.rename(os.path.join(root, name), os.path.join(root, new_name))
            print(f"Renamed file {name} to {new_name}")
            
    for name in dirs:
        new_name = name
        for old, new in replacements.items():
            new_name = new_name.replace(old, new)
        if new_name != name:
            os.rename(os.path.join(root, name), os.path.join(root, new_name))
            print(f"Renamed dir {name} to {new_name}")

# Finally, rename the root directory
new_project_dir = project_dir.replace("WinUI3", "WindowsEmbed")
os.rename(project_dir, new_project_dir)
print(f"Renamed {project_dir} to {new_project_dir}")

