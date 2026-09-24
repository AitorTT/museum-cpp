// Screenshots the museum from vantage points inside real rooms, to check
// texture tiling, sky, fog and tonemapping together.
//
// These are auto-selected: the pose is pulled back from the far wall by 4m
// (the wall inner face is at +/-4.925, so 3.4 is comfortably clear of it).
import { chromium } from "playwright";

const URL = process.env.TEST_URL ?? "http://localhost:8000/museum.html?v=b3e";
const browser = await chromium.launch({ channel: "msedge", headless: true });
const page = await browser.newPage({ viewport: { width: 1280, height: 720 } });
await page.goto(URL, { waitUntil: "load", timeout: 60000 });
await page.waitForFunction(() => typeof _museumSetPose === "function", { timeout: 60000 });
await page.click("#overlay");
await page.waitForTimeout(800);

async function shot(name, x, y, z, yaw, pitch) {
  await page.evaluate(
    ([px, py, pz, pyaw, ppitch]) => _museumSetPose(px, py, pz, pyaw, ppitch),
    [x, y, z, yaw, pitch],
  );
  await page.waitForTimeout(500);
  const pos = await page.evaluate(() => [
    _museumPlayerX(), _museumPlayerY(), _museumPlayerZ(), _museumOnGround(),
  ]);
  await page.screenshot({ path: `batch3-${name}.png` });
  console.log(`${name}: pos=(${pos[0].toFixed(2)}, ${pos[1].toFixed(2)}, ${pos[2].toFixed(2)}) onGround=${pos[3]}`);
}

// Floor 1 top is -2.925, so the box centre rests at -1.75.
const Y = -1.75;

// Stand in the corner of room (-10,10), look across it and out the xp doorway
// toward room (0,10): checks texture, door cut, and depth.
await shot("room", -13.0, Y, 10.0, -Math.PI / 2, 0.0);
// Look along the zp/zn run through rooms (-10,0) -> (-10,-10) -> (-10,-20).
await shot("corridor", -10.0, Y, 5.0, Math.PI, 0.0);
// Look up at a ceiling from inside a room.
await shot("ceiling", -10.0, Y, 10.0, 0.0, 0.7);
// Stand near a doorway looking through it, long view for fog.
await shot("doorway", -5.0, Y, 10.0, -Math.PI / 2, 0.0);
// Top floor room (-10,20) on floor 4 (y offset 18): look up past the ceiling
// to catch the sky if any seam exists, and check fog at height.
await shot("topfloor", -10.0, 16.25, 17.0, Math.PI, 0.4);

await browser.close();
