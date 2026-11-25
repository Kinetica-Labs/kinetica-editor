"""kpkg - a scene in one file, as USDZ is to USD: a zip holding scene.kscene and every file it refers to (models,
textures) under files/, the scene's paths rewritten to them. An OBJ that draws on files of its own - its material library
and the library's pictures - goes in a folder of its own with them, under the names it calls them by. Read: unpacked beside the package into <name>-files/ and
opened there - a scene like any other (Ctrl+S saves it there, «Экспорт…» packs it again). A member that would land
outside that folder (an absolute path, a ..) is refused: a package from elsewhere writes nowhere else. Standard library
only (zipfile)."""
import os
import re
import zipfile

FILE = re.compile(r'(\bfile ")([^"]*)(")')  # every path the scene file holds: a model's, a texture's


def plural(n, one, few, many):
    if n % 10 == 1 and n % 100 != 11:
        return f"{n} {one}"
    return f"{n} {few if 2 <= n % 10 <= 4 and not 12 <= n % 100 <= 14 else many}"


def sidecars(model):
    """The files an OBJ draws on beside itself: its material libraries (mtllib) and their pictures (map_Kd), each as the
    name it calls it by - from the OBJ's folder. Without them it would come grey."""
    if not model.lower().endswith(".obj"):
        return []
    with open(model, encoding="utf-8", errors="replace") as f:
        libraries = [line[7:].strip() for line in f if line.startswith("mtllib ")]
    found = []
    for library in libraries:
        found.append(library)
        try:
            with open(os.path.join(os.path.dirname(model), library), encoding="utf-8", errors="replace") as f:
                for words in (line.split() for line in f):
                    if words[:1] == ["map_Kd"] and len(words) > 1:  # its file is the last word, past a map's options
                        found.append(os.path.join(os.path.dirname(library), words[-1].replace("\\", "/")))
        except OSError:
            pass  # a library that cannot be read is said missing below
    return found


def save(scene, path):
    """The scene and the files it refers to, one zip; the scene itself unchanged."""
    whole = scene.ed.scene_get()
    text, folder = whole["text"], whole.get("folder") or os.getcwd()
    packed, missing = {}, set()
    beside = []  # (source, inside): a model's own files, packed where it looks for them

    def into(m):
        name = m.group(2)
        if not name:
            return m.group(0)
        source = os.path.abspath(name if os.path.isabs(name) else os.path.join(folder, name))
        if not os.path.isfile(source):
            missing.add(name)
            return m.group(0)
        if source not in packed:
            stem, ext = os.path.splitext(os.path.basename(source))
            own = sidecars(source)
            n = 1
            while True:  # two files of one name from two folders: both kept
                tag = stem if n == 1 else f"{stem}-{n}"
                inside = f"files/{tag}/{stem}{ext}" if own else f"files/{tag}{ext}"
                if not any(p == inside or p.startswith(f"files/{tag}/") for p in packed.values()):
                    break
                n += 1
            packed[source] = inside
            for name in own:
                relative = os.path.normpath(name)
                file = os.path.join(os.path.dirname(source), relative)
                if relative.startswith("..") or os.path.isabs(relative) or not os.path.isfile(file):
                    missing.add(name)  # outside the model's folder, or not there
                else:
                    beside.append((file, f"files/{tag}/{relative.replace(os.sep, '/')}"))
        return m.group(1) + packed[source] + m.group(3)

    text = FILE.sub(into, text)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("scene.kscene", text)
        for source, inside in list(packed.items()) + beside:
            z.write(source, inside)
    note = f"«{os.path.basename(path)}»: сцена и {plural(len(packed) + len(beside), 'файл', 'файла', 'файлов')}"
    if missing:
        note += "; нет и не взяты: " + ", ".join(sorted(missing))
    return {"files": len(packed) + len(beside), "missing": sorted(missing), "note": note}


def load(scene, path):
    """The package unpacked beside it into <name>-files/ and its scene opened from there (one undo step back)."""
    stem = os.path.splitext(os.path.basename(path))[0]
    target = os.path.join(os.path.dirname(os.path.abspath(path)), stem + "-files")
    refused = []
    with zipfile.ZipFile(path) as z:
        if "scene.kscene" not in z.namelist():
            raise ValueError(f"{os.path.basename(path)}: в пакете нет scene.kscene")
        safe = []
        for member in z.namelist():
            landing = os.path.abspath(os.path.join(target, member))
            (safe if landing.startswith(os.path.abspath(target) + os.sep) else refused).append(member)
        z.extractall(target, members=safe)
    scene.ed.scene_open(file=os.path.join(target, "scene.kscene"))
    note = f"«{stem}»: распакован в {target}"
    if refused:
        note += "; не распакованы (вне папки пакета): " + ", ".join(refused)
    return {"folder": target, "refused": refused, "note": note}
