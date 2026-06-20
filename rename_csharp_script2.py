import os

replacements = {
    "winui3": "windows_embed",
    "WinUI3": "WindowsEmbed",
    "WINUI3": "WINDOWS_EMBED"
}

project_dir = "platform/windows/WinUI3_Project"

# Rename files avoiding .vs
for root, dirs, files in os.walk(project_dir, topdown=False):
    if ".vs" in root or "obj" in root or "bin" in root:
        continue

    for name in files:
        new_name = name
        for old, new in replacements.items():
            new_name = new_name.replace(old, new)
        if new_name != name:
            os.rename(os.path.join(root, name), os.path.join(root, new_name))
            print(f"Renamed file {name} to {new_name}")
            
    for name in dirs:
        # Ignore renaming .vs, bin, obj themselves
        if name in [".vs", "bin", "obj"]:
            continue
        new_name = name
        for old, new in replacements.items():
            new_name = new_name.replace(old, new)
        if new_name != name:
            os.rename(os.path.join(root, name), os.path.join(root, new_name))
            print(f"Renamed dir {name} to {new_name}")

# Rename the root directory
new_project_dir = project_dir.replace("WinUI3", "WindowsEmbed")
os.rename(project_dir, new_project_dir)
print(f"Renamed {project_dir} to {new_project_dir}")
