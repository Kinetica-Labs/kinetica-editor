"""kpkg - a scene in one file, as USDZ is to USD: a zip holding scene.kscene and every file it refers to (models,
textures) under files/, the scene's paths rewritten to them. Read: unpacked beside the package into <name>-files/ and
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


def save(scene, path):
    """The scene and the files it refers to, one zip; the scene itself unchanged."""
    whole = scene.ed.scene_get()
    text, folder = whole["text"], whole.get("folder") or os.getcwd()
    packed, missing = {}, set()

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
            inside, n = f"files/{stem}{ext}", 2
            while inside in packed.values():  # two files of one name from two folders: both kept
                inside, n = f"files/{stem}-{n}{ext}", n + 1
            packed[source] = inside
        return m.group(1) + packed[source] + m.group(3)

    text = FILE.sub(into, text)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("scene.kscene", text)
        for source, inside in packed.items():
            z.write(source, inside)
    note = f"«{os.path.basename(path)}»: сцена и {plural(len(packed), 'файл', 'файла', 'файлов')}"
    if missing:
        note += "; нет и не взяты: " + ", ".join(sorted(missing))
    return {"files": len(packed), "missing": sorted(missing), "note": note}


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
