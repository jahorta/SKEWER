VARYING vec4 vertexColor;
VARYING vec2 triangleCoordinate;
VARYING vec3 fieldPosition;
VARYING float selectionAmount;

void MAIN()
{
    vertexColor = COLOR;
    triangleCoordinate = UV0;
    fieldPosition = VERTEX;
    selectionAmount = UV1.x;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
    POSITION.z -= depthOffset * POSITION.w;
}
