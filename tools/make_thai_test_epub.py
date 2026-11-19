#!/usr/bin/env python3
"""A small Thai test book for the Toybox reader. All text written for this test."""
import zipfile, sys

TITLE = "ทดสอบภาษาไทย Thai test"

ch1 = """<h1>ทดสอบภาษาไทย</h1>
<p>หนังสือเล่มนี้ใช้ทดสอบการแสดงผลภาษาไทยบน Sticky ดูว่าสระไม่ลอย วรรณยุกต์ไม่จม และเส้นตัวอักษรหนาเท่ากันทุกตัว</p>

<h2>๑. พยัญชนะหางสูง ป ฝ ฟ ฬ</h2>
<p>ปั้น ปี่ ปู่ ป้า ปิ่น ปื้น ปั๊ม ป๋า</p>
<p>ฝั่ง ฝี ฝ้าย ฝึก ฝั่น ฝื่น</p>
<p>ฟ้า ฟื้น ฟั่น ฟ้อง ฟิ้ว ฟั๊ก</p>
<p>จุฬา กีฬา ฬ่า ฬี่</p>

<h2>๒. วรรณยุกต์บนพยัญชนะเปล่า</h2>
<p>ก่า ก้า ก๊า ก๋า ก์</p>
<p>ไม่ ได้ ใช่ แม่ พ่อ น้อง พี่ ป้า</p>
<p>เก่ง เก้า เก๊ เก๋ ต่อ ต้อง ห้า ห่าง</p>

<h2>๓. สระบนกับวรรณยุกต์</h2>
<p>ที่ นี่ ดื่ม สิ้น ทั้ง ตื่น ยิ้ม มั่น คิ้ว ปี๊บ</p>
<p>เกี๊ยว เขี้ยว เมื่อ เพื่อน เรื่อง เดี๋ยว</p>

<h2>๔. สระอำ</h2>
<p>น้ำ ค่ำ ซ้ำ ล้ำ ทำ จำ ปำ ฟ้ำ ต่ำ ก้ำกึ่ง</p>

<h2>๕. สระล่างใต้ ญ ฐ ฎ ฏ</h2>
<p>กตัญญู บุญ ญุ ญู ฐุ ฐู ฎุ ฎู ฏุ ฏู</p>
<p>ฐาน ญาติ ฎีกา ปฏิทิน กฎ ปรากฏ</p>
<p>ปุ่ม ปู ฟุ้ง ฝุ่น ภู ฦ ฤดู ฤๅษี</p>

<h2>๖. การันต์ ไม้ยมก ไปยาลน้อย ตัวเลขไทย</h2>
<p>ศาสตร์ จันทร์ สัตว์ พระพุทธเจ้า สิทธิ์ ลักษณ์</p>
<p>เด็กๆ เล่นกันทุกวันๆ กรุงเทพฯ ฯลฯ</p>
<p>๑๒๓๔๕๖๗๘๙๐ และ 1234567890</p>

<h2>๗. ไทยปนอังกฤษ</h2>
<p>อ่าน EPUB บน Sticky ด้วย Toybox v2 แล้วซิงค์กับ Readest ได้ “ทันที” — ลองดูสิ</p>
<p><b>ตัวหนาภาษาไทย</b> กับ <b>Bold English</b> ในบรรทัดเดียวกัน</p>
"""

ch2 = """<h1>เรื่องสั้นสำหรับทดสอบ</h1>
<p>คืนนั้นฝนตกปรอยๆ ตั้งแต่หัวค่ำ น้องเล็กนั่งอยู่ริมหน้าต่าง ถือเครื่องอ่านหนังสือเครื่องเล็กไว้ในมือ หน้าจอสีเทาอ่อนสะท้อนแสงไฟจากโคมข้างเตียง ตัวหนังสือคมชัดเหมือนพิมพ์ลงบนกระดาษจริงๆ</p>
<p>เธอเลื่อนหน้าไปเรื่อยๆ อ่านเรื่องของนักเดินทางคนหนึ่งที่ข้ามภูเขาฝ้ายขาวไปยังหมู่บ้านริมฝั่งน้ำ ที่นั่นมีชาวบ้านปลูกข้าว ทอผ้า และเลี้ยงปลาในบ่อหลังบ้าน ทุกเช้าจะมีเสียงไก่ขันปลุกให้ตื่นก่อนฟ้าสาง</p>
<p>“พรุ่งนี้ไปตลาดกันไหม” พี่ชายถามจากอีกห้อง น้องเล็กตอบว่า “ไปสิ แต่ขออ่านให้จบบทนี้ก่อนนะ” แล้วก็ก้มหน้าอ่านต่อจนดึก ลืมไปเลยว่าน้ำชาในแก้วเย็นชืดไปนานแล้ว</p>
<p>เมื่อถึงหน้าสุดท้ายของบท เธอปิดเครื่องอย่างระมัดระวัง วางไว้ข้างหมอน แล้วหลับไปพร้อมกับภาพของหมู่บ้านริมน้ำที่ยังลอยอยู่ในความคิด</p>
<p>บรรทัดนี้ยาวเป็นพิเศษเพื่อทดสอบการตัดคำภาษาไทยซึ่งไม่มีช่องว่างระหว่างคำและต้องตัดบรรทัดให้ถูกตำแหน่งโดยไม่แยกสระหรือวรรณยุกต์ออกจากพยัญชนะของมันเลยแม้แต่ตัวเดียว</p>
"""

def xhtml(title, body):
    return f"""<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE html>
<html xmlns="http://www.w3.org/1999/xhtml" xml:lang="th" lang="th">
<head><meta charset="utf-8"/><title>{title}</title></head>
<body>
{body}</body>
</html>
"""

opf = f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="3.0" unique-identifier="id" xml:lang="th">
 <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
  <dc:identifier id="id">urn:uuid:7d3c1f0e-5a2b-4c11-9e8a-th0test00001</dc:identifier>
  <dc:title>{TITLE}</dc:title>
  <dc:language>th</dc:language>
  <dc:creator>Toybox</dc:creator>
  <meta property="dcterms:modified">2026-11-19T00:00:00Z</meta>
 </metadata>
 <manifest>
  <item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>
  <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  <item id="c1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
  <item id="c2" href="ch2.xhtml" media-type="application/xhtml+xml"/>
 </manifest>
 <spine toc="ncx">
  <itemref idref="c1"/>
  <itemref idref="c2"/>
 </spine>
</package>
"""

nav = xhtml("สารบัญ", """<nav epub:type="toc" xmlns:epub="http://www.idpf.org/2007/ops"><h1>สารบัญ</h1><ol>
<li><a href="ch1.xhtml">ทดสอบภาษาไทย</a></li>
<li><a href="ch2.xhtml">เรื่องสั้นสำหรับทดสอบ</a></li>
</ol></nav>
""")

ncx = f"""<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1">
 <head><meta name="dtb:uid" content="urn:uuid:7d3c1f0e-5a2b-4c11-9e8a-th0test00001"/></head>
 <docTitle><text>{TITLE}</text></docTitle>
 <navMap>
  <navPoint id="p1" playOrder="1"><navLabel><text>ทดสอบภาษาไทย</text></navLabel><content src="ch1.xhtml"/></navPoint>
  <navPoint id="p2" playOrder="2"><navLabel><text>เรื่องสั้นสำหรับทดสอบ</text></navLabel><content src="ch2.xhtml"/></navPoint>
 </navMap>
</ncx>
"""

container = """<?xml version="1.0" encoding="utf-8"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
 <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>
"""

out = sys.argv[1]
with zipfile.ZipFile(out, "w") as z:
    z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip", compress_type=zipfile.ZIP_STORED)
    for name, data in [("META-INF/container.xml", container), ("OEBPS/content.opf", opf),
                       ("OEBPS/nav.xhtml", nav), ("OEBPS/toc.ncx", ncx),
                       ("OEBPS/ch1.xhtml", xhtml("ทดสอบภาษาไทย", ch1)),
                       ("OEBPS/ch2.xhtml", xhtml("เรื่องสั้น", ch2))]:
        z.writestr(name, data.encode("utf-8"), compress_type=zipfile.ZIP_DEFLATED)
print(out)
