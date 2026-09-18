const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const sourceRoot = path.resolve(__dirname, '..', '..', '..', '..', 'Downloads', 'KgSem4_2026-b312c95d11bb8af7f6a30371eb1d72f6ea0c3ddc', 'KgSem4_2026-b312c95d11bb8af7f6a30371eb1d72f6ea0c3ddc', 'KG_Sem4_Laba3', 'assets');
const targetRoot = path.resolve(__dirname, '..', 'Lab_4', 'Project1', 'Assets', 'Earth');

function readProperty(buffer, offset) {
    const type = String.fromCharCode(buffer[offset++]);
    const scalarSizes = { Y: 2, C: 1, I: 4, F: 4, D: 8, L: 8 };
    if (scalarSizes[type]) {
        const size = scalarSizes[type];
        const readers = { Y: 'readInt16LE', C: 'readUInt8', I: 'readInt32LE', F: 'readFloatLE', D: 'readDoubleLE', L: 'readBigInt64LE' };
        return { value: buffer[readers[type]](offset), offset: offset + size };
    }
    if (type === 'S' || type === 'R') {
        const size = buffer.readUInt32LE(offset);
        const start = offset + 4;
        return { value: type === 'S' ? buffer.toString('utf8', start, start + size) : buffer.subarray(start, start + size), offset: start + size };
    }
    if ('fdlib'.includes(type)) {
        const count = buffer.readUInt32LE(offset);
        const encoding = buffer.readUInt32LE(offset + 4);
        const length = buffer.readUInt32LE(offset + 8);
        let data = buffer.subarray(offset + 12, offset + 12 + length);
        if (encoding === 1) data = zlib.inflateSync(data);
        if (encoding !== 0 && encoding !== 1) throw new Error(`Unsupported FBX array encoding: ${encoding}`);
        const bytes = { f: 4, d: 8, l: 8, i: 4, b: 1 }[type];
        if (data.length !== count * bytes) throw new Error('FBX array has an unexpected length');
        const readers = { f: 'readFloatLE', d: 'readDoubleLE', l: 'readBigInt64LE', i: 'readInt32LE', b: 'readUInt8' };
        const values = Array.from({ length: count }, (_, i) => data[readers[type]](i * bytes));
        return { value: values, offset: offset + 12 + length };
    }
    throw new Error(`Unsupported FBX property type: ${type}`);
}

function readNodes(buffer, offset, end) {
    const nodes = [];
    while (offset < end) {
        const nodeEnd = buffer.readUInt32LE(offset);
        if (nodeEnd === 0) break;
        const propertyCount = buffer.readUInt32LE(offset + 4);
        const propertyBytes = buffer.readUInt32LE(offset + 8);
        const nameLength = buffer[offset + 12];
        const name = buffer.toString('utf8', offset + 13, offset + 13 + nameLength);
        let cursor = offset + 13 + nameLength;
        const properties = [];
        for (let i = 0; i < propertyCount; ++i) {
            const property = readProperty(buffer, cursor);
            properties.push(property.value);
            cursor = property.offset;
        }
        const children = readNodes(buffer, cursor, nodeEnd - 13);
        nodes.push({ name, properties, children });
        offset = nodeEnd;
    }
    return nodes;
}

function findChild(node, name) {
    return node.children.find(child => child.name === name);
}

function nodeArray(node, name) {
    const child = findChild(node, name);
    if (!child || !Array.isArray(child.properties[0])) throw new Error(`Earth.fbx does not contain ${name}`);
    return child.properties[0];
}

function layerValues(geometry, layerName, valueName, indexName) {
    const layer = findChild(geometry, layerName);
    if (!layer) throw new Error(`Earth.fbx does not contain ${layerName}`);
    const mapping = findChild(layer, 'MappingInformationType').properties[0];
    const reference = findChild(layer, 'ReferenceInformationType').properties[0];
    return { values: nodeArray(layer, valueName), indices: findChild(layer, indexName)?.properties[0], mapping, reference };
}

function valueIndex(layer, controlPoint, polygonVertex) {
    let index = layer.mapping === 'ByControlPoint' ? controlPoint : polygonVertex;
    if (layer.reference === 'IndexToDirect') index = layer.indices[index];
    return index;
}

function writeObj() {
    const fbx = fs.readFileSync(path.join(sourceRoot, 'Earth.fbx'));
    if (fbx.toString('ascii', 0, 20) !== 'Kaydara FBX Binary  ') throw new Error('Expected binary FBX');
    const version = fbx.readUInt32LE(23);
    if (version >= 7500) throw new Error(`Unsupported FBX version ${version}`);
    const nodes = readNodes(fbx, 27, fbx.length);
    const objects = nodes.find(node => node.name === 'Objects');
    const geometry = objects.children.find(node => node.name === 'Geometry' && node.properties[2] === 'Mesh');
    if (!geometry) throw new Error('Earth mesh was not found');

    const positions = nodeArray(geometry, 'Vertices');
    const polygonIndices = nodeArray(geometry, 'PolygonVertexIndex');
    const normals = layerValues(geometry, 'LayerElementNormal', 'Normals', 'NormalsIndex');
    const uvs = layerValues(geometry, 'LayerElementUV', 'UV', 'UVIndex');
    const lines = ['mtllib Earth.mtl', 'usemtl Earth'];
    const unique = new Map();
    const faces = [];
    let polygon = [];
    let polygonVertex = 0;

    function emitVertex(controlPoint) {
        const normalIndex = valueIndex(normals, controlPoint, polygonVertex);
        const uvIndex = valueIndex(uvs, controlPoint, polygonVertex);
        const key = `${controlPoint}/${uvIndex}/${normalIndex}`;
        let vertex = unique.get(key);
        if (!vertex) {
            vertex = unique.size + 1;
            unique.set(key, vertex);
            lines.push(`v ${positions[controlPoint * 3]} ${positions[controlPoint * 3 + 1]} ${positions[controlPoint * 3 + 2]}`);
        }
        return { vertex, uvIndex, normalIndex };
    }

    for (const rawIndex of polygonIndices) {
        const isLast = rawIndex < 0;
        const controlPoint = isLast ? -rawIndex - 1 : rawIndex;
        polygon.push(emitVertex(controlPoint));
        ++polygonVertex;
        if (isLast) {
            for (let i = 1; i + 1 < polygon.length; ++i) faces.push([polygon[0], polygon[i], polygon[i + 1]]);
            polygon = [];
        }
    }

    for (let i = 0; i < uvs.values.length; i += 2) lines.push(`vt ${uvs.values[i]} ${1.0 - uvs.values[i + 1]}`);
    for (let i = 0; i < normals.values.length; i += 3) lines.push(`vn ${normals.values[i]} ${normals.values[i + 1]} ${normals.values[i + 2]}`);
    for (const face of faces) lines.push(`f ${face.map(v => `${v.vertex}/${v.uvIndex + 1}/${v.normalIndex + 1}`).join(' ')}`);
    fs.writeFileSync(path.join(targetRoot, 'Earth.obj'), `${lines.join('\n')}\n`);
    fs.writeFileSync(path.join(targetRoot, 'Earth.mtl'), 'newmtl Earth\nmap_Kd Earth_ALB.bmp\nnorm Earth_NORM.bmp\ndisp Earth_HEIGHT.bmp\n');
}

function writeBmp(name) {
    const dds = fs.readFileSync(path.join(sourceRoot, 'textures', 'earth', `${name}.dds`));
    if (dds.toString('ascii', 0, 4) !== 'DDS ') throw new Error(`${name} is not DDS`);
    const height = dds.readUInt32LE(12);
    const width = dds.readUInt32LE(16);
    if (dds.toString('ascii', 84, 88) !== 'DXT1') throw new Error(`${name} must use DXT1 compression`);
    const source = dds.subarray(128);
    const outputWidth = width / 2;
    const outputHeight = height / 2;
    if (!Number.isInteger(outputWidth) || !Number.isInteger(outputHeight)) throw new Error(`${name} dimensions must be even`);
    const pixels = Buffer.alloc(outputWidth * outputHeight * 4);

    function color565(value) {
        return [
            Math.round(((value >> 11) & 31) * 255 / 31),
            Math.round(((value >> 5) & 63) * 255 / 63),
            Math.round((value & 31) * 255 / 31),
            255
        ];
    }

    function dxtColor(x, y) {
        const blockOffset = (Math.floor(y / 4) * Math.ceil(width / 4) + Math.floor(x / 4)) * 8;
        const first = source.readUInt16LE(blockOffset);
        const second = source.readUInt16LE(blockOffset + 2);
        const colors = [color565(first), color565(second)];
        if (first > second) {
            colors.push(colors[0].map((value, i) => i === 3 ? 255 : Math.round((2 * value + colors[1][i]) / 3)));
            colors.push(colors[0].map((value, i) => i === 3 ? 255 : Math.round((value + 2 * colors[1][i]) / 3)));
        } else {
            colors.push(colors[0].map((value, i) => i === 3 ? 255 : Math.round((value + colors[1][i]) / 2)));
            colors.push([0, 0, 0, 0]);
        }
        const selector = source.readUInt32LE(blockOffset + 4);
        return colors[(selector >>> (2 * ((y % 4) * 4 + (x % 4))) & 3)];
    }

    for (let y = 0; y < outputHeight; ++y) {
        for (let x = 0; x < outputWidth; ++x) {
            const color = dxtColor(x * 2, y * 2);
            const pixelOffset = (y * outputWidth + x) * 4;
            pixels[pixelOffset] = color[2];
            pixels[pixelOffset + 1] = color[1];
            pixels[pixelOffset + 2] = color[0];
            pixels[pixelOffset + 3] = color[3];
        }
    }
    const header = Buffer.alloc(54);
    header.write('BM');
    header.writeUInt32LE(header.length + pixels.length, 2);
    header.writeUInt32LE(54, 10);
    header.writeUInt32LE(40, 14);
    header.writeInt32LE(outputWidth, 18);
    header.writeInt32LE(outputHeight, 22);
    header.writeUInt16LE(1, 26);
    header.writeUInt16LE(32, 28);
    header.writeUInt32LE(pixels.length, 34);
    const flipped = Buffer.alloc(pixels.length);
    const rowSize = outputWidth * 4;
    for (let y = 0; y < outputHeight; ++y) pixels.copy(flipped, y * rowSize, (outputHeight - y - 1) * rowSize, (outputHeight - y) * rowSize);
    fs.writeFileSync(path.join(targetRoot, `${name}.bmp`), Buffer.concat([header, flipped]));
}

fs.mkdirSync(targetRoot, { recursive: true });
writeObj();
['Earth_ALB', 'Earth_NORM', 'Earth_HEIGHT'].forEach(writeBmp);
console.log(`Created Earth assets in ${targetRoot}`);
