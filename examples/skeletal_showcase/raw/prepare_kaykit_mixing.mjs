import fs from "node:fs";

const [source, destination] = process.argv.slice(2);
if (!source || !destination) throw new Error("usage: prepare_kaykit_mixing.mjs <source.glb> <destination.glb>");

const bytes = fs.readFileSync(source);
const jsonLength = bytes.readUInt32LE(12);
const json = JSON.parse(bytes.subarray(20, 20 + jsonLength).toString().replace(/\0+$/, ""));
const jsonEnd = 20 + ((jsonLength + 3) & ~3);
const binLength = bytes.readUInt32LE(jsonEnd);
const bin = bytes.subarray(jsonEnd + 8, jsonEnd + 8 + binLength);

const rootNode = json.nodes.findIndex((node) => node.name === "root");
const rigNode = json.nodes.findIndex((node) => node.name === "Rig");
if (rootNode < 0 || rigNode < 0) throw new Error("KayKit root/Rig nodes not found");
json.nodes[rigNode].children = json.nodes[rigNode].children.filter((child) => child !== rootNode);
json.scenes[json.scene ?? 0].nodes.push(rootNode);
json.skins[0].skeleton = rootNode;

for (const animation of json.animations) {
  animation.channels = animation.channels.filter(
    (channel) => channel.target.path === "rotation" || (channel.target.node === rootNode && channel.target.path === "translation"),
  );
}
json.asset.generator = `${json.asset.generator}; Neotolis mixing showcase: rotation-only joints, detached root`;

const encoded = Buffer.from(JSON.stringify(json));
const paddedJsonLength = (encoded.length + 3) & ~3;
const totalLength = 12 + 8 + paddedJsonLength + 8 + bin.length;
const output = Buffer.alloc(totalLength, 0x20);
output.writeUInt32LE(0x46546c67, 0);
output.writeUInt32LE(2, 4);
output.writeUInt32LE(totalLength, 8);
output.writeUInt32LE(paddedJsonLength, 12);
output.writeUInt32LE(0x4e4f534a, 16);
encoded.copy(output, 20);
const binHeader = 20 + paddedJsonLength;
output.writeUInt32LE(bin.length, binHeader);
output.writeUInt32LE(0x004e4942, binHeader + 4);
bin.copy(output, binHeader + 8);
fs.writeFileSync(destination, output);
