#!/usr/bin/env python3
import os
import sys
import subprocess
import shutil
import re

if len(sys.argv) < 2:
    print("Usage: fix_dependencies.py <path_to_app_bundle>")
    sys.exit(1)

app_path = sys.argv[1]
frameworks_dir = os.path.join(app_path, "Contents", "Frameworks")

if not os.path.exists(frameworks_dir):
    os.makedirs(frameworks_dir)

# Regexp to match lines from otool -L
# Example: "	/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib (compatibility version 1.0.0, current version 1.2.0)"
otool_re = re.compile(r'^\s*([^\s]+)\s+\(')

def get_dependencies(filepath):
    try:
        result = subprocess.run(["otool", "-L", filepath], capture_output=True, text=True, check=True)
        deps = []
        for line in result.stdout.splitlines():
            match = otool_re.match(line)
            if match:
                dep_path = match.group(1)
                # Skip if it is the file itself or matches the file path
                if dep_path != filepath and os.path.basename(dep_path) != os.path.basename(filepath):
                    deps.append(dep_path)
        return deps
    except Exception as e:
        print(f"Error reading dependencies for {filepath}: {e}")
        return []

def is_macho(filepath):
    try:
        res = subprocess.run(["file", filepath], capture_output=True, text=True, check=True)
        return "Mach-O" in res.stdout
    except Exception:
        return False

def should_fix_id(lib_id):
    if lib_id.startswith("/usr/lib/") or lib_id.startswith("/System/Library/"):
        return False
    if lib_id.startswith("@executable_path/") or lib_id.startswith("@loader_path/"):
        return False
    return True

def fix_binary_id(filepath):
    try:
        res = subprocess.run(["otool", "-D", filepath], capture_output=True, text=True, check=True)
        lines = res.stdout.splitlines()
        if len(lines) >= 2:
            current_id = lines[1].strip()
            if should_fix_id(current_id):
                idx = filepath.find("Contents/")
                if idx != -1:
                    rel_path = filepath[idx + len("Contents/"):]
                    new_id = f"@executable_path/../{rel_path}"
                    print(f"  Fixing library ID: {current_id} -> {new_id}")
                    subprocess.run(["install_name_tool", "-id", new_id, filepath])
    except Exception as e:
        print(f"Error checking/fixing library ID for {filepath}: {e}")

def get_binary_rpaths(filepath):
    try:
        res = subprocess.run(["otool", "-l", filepath], capture_output=True, text=True, check=True)
        rpaths = []
        lines = res.stdout.splitlines()
        i = 0
        while i < len(lines):
            line = lines[i].strip()
            if line == "cmd LC_RPATH":
                if i + 2 < len(lines):
                    path_line = lines[i + 2].strip()
                    if path_line.startswith("path "):
                        path_str = path_line[5:].strip()
                        # remove " (offset ...)"
                        path_str = re.sub(r'\s+\(offset\s+\d+\)$', '', path_str)
                        rpaths.append(path_str)
            i += 1
        return rpaths
    except Exception as e:
        print(f"Error reading RPATHs for {filepath}: {e}")
        return []

def resolve_rpath_variables(rpath, filepath):
    executable_path = os.path.normpath(os.path.join(app_path, "Contents", "MacOS"))
    loader_path = os.path.normpath(os.path.dirname(filepath))
    resolved = rpath
    if "@executable_path" in resolved:
        resolved = resolved.replace("@executable_path", executable_path)
    if "@loader_path" in resolved:
        resolved = resolved.replace("@loader_path", loader_path)
    return os.path.normpath(resolved)

def resolve_dependency_on_host(dep, filepath):
    # Skip system libraries
    if dep.startswith("/usr/lib/") or dep.startswith("/System/Library/"):
        return None
        
    # If it is an absolute path that exists on the host
    if dep.startswith("/"):
        if os.path.exists(dep):
            return dep
        return None
        
    # If it starts with @rpath/, search in bundle first, then dynamic rpaths, then host search paths
    if dep.startswith("@rpath/"):
        rel_path = dep.replace("@rpath/", "")
        
        # Check inside the bundle's Frameworks folder first
        bundle_candidate = os.path.join(frameworks_dir, rel_path)
        if os.path.exists(bundle_candidate):
            return bundle_candidate
            
        # Check binary's own RPATHs (e.g. build directory paths)
        for rpath in get_binary_rpaths(filepath):
            resolved_rpath = resolve_rpath_variables(rpath, filepath)
            candidate = os.path.join(resolved_rpath, rel_path)
            if os.path.exists(candidate):
                return candidate
            
        # Check host search paths
        for host_dir in ["/opt/homebrew/lib", "/usr/local/lib"]:
            candidate = os.path.join(host_dir, rel_path)
            if os.path.exists(candidate):
                return candidate
        return None
        
    # If it starts with @executable_path/ or @loader_path/, resolve relative to bundle
    executable_path = os.path.join(app_path, "Contents", "MacOS")
    loader_path = os.path.dirname(filepath)
    
    if dep.startswith("@executable_path/"):
        bundle_candidate = os.path.normpath(dep.replace("@executable_path", executable_path))
        if os.path.exists(bundle_candidate):
            return bundle_candidate
    elif dep.startswith("@loader_path/"):
        bundle_candidate = os.path.normpath(dep.replace("@loader_path", loader_path))
        if os.path.exists(bundle_candidate):
            return bundle_candidate
            
    return None


def copy_external_library(dep_path):
    # Check if it is a framework
    idx = dep_path.find(".framework/")
    if idx != -1:
        # Extract framework folder name (e.g. QtVirtualKeyboard.framework)
        framework_dir_name = os.path.basename(dep_path[:idx + len(".framework")])
        src_framework_dir = dep_path[:idx + len(".framework")]
        dest_framework_dir = os.path.join(frameworks_dir, framework_dir_name)
        
        if not os.path.exists(dest_framework_dir):
            print(f"  Copying framework: {src_framework_dir} -> {dest_framework_dir}")
            try:
                shutil.copytree(src_framework_dir, dest_framework_dir, symlinks=True)
                # Ensure all files inside are writable
                for root, dirs, files in os.walk(dest_framework_dir):
                    for file in files:
                        full_f = os.path.join(root, file)
                        if not os.path.islink(full_f):
                            os.chmod(full_f, 0o755)
            except Exception as e:
                print(f"  Error copying framework {src_framework_dir}: {e}")
                return None
                
        # Return path to the corresponding binary inside the copied framework
        rel_path = dep_path[idx + len(".framework/"):]
        return os.path.join(dest_framework_dir, rel_path)
    else:
        # Standalone dylib
        lib_name = os.path.basename(dep_path)
        dest_path = os.path.join(frameworks_dir, lib_name)
        
        if not os.path.exists(dest_path):
            print(f"  Copying library: {dep_path} -> {dest_path}")
            try:
                shutil.copy2(dep_path, dest_path)
                os.chmod(dest_path, 0o755)
            except Exception as e:
                print(f"  Error copying library {dep_path}: {e}")
                return None
        return dest_path

def fix_binary(filepath):
    print(f"Processing: {filepath}")
    fix_binary_id(filepath)
    deps = get_dependencies(filepath)
    
    for dep in deps:
        resolved = resolve_dependency_on_host(dep, filepath)
        if not resolved:
            continue
            
        # Check if the resolved path is outside the bundle
        if not resolved.startswith(app_path):
            copied_path = copy_external_library(resolved)
            if not copied_path:
                continue
            resolved = copied_path
            
        # Now the dependency is inside the bundle
        idx = resolved.find("Contents/")
        if idx != -1:
            rel_path = resolved[idx + len("Contents/"):]
            new_dep_path = f"@executable_path/../{rel_path}"
            
            if dep != new_dep_path:
                print(f"  Changing dependency: {dep} -> {new_dep_path}")
                subprocess.run(["install_name_tool", "-change", dep, new_dep_path, filepath])

def main():
    # Iterate multiple times to resolve transitive dependencies of copied libraries/frameworks
    for iteration in range(5):
        print(f"\n--- Scan Iteration {iteration + 1} ---")
        binaries_to_fix = []
        for root, dirs, files in os.walk(app_path):
            for file in files:
                full_path = os.path.join(root, file)
                if os.path.islink(full_path):
                    continue
                if is_macho(full_path):
                    binaries_to_fix.append(full_path)
                    
        for binary in binaries_to_fix:
            fix_binary(binary)

if __name__ == "__main__":
    main()
