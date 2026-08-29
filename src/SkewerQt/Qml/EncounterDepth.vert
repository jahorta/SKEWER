VARYING vec4 vertexColor;
VARYING vec2 triangleCoordinate;

void MAIN()
{
    vertexColor = COLOR;
    triangleCoordinate = UV0;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
    POSITION.z -= depthOffset * POSITION.w;
}
