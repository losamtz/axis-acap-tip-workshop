# Parameter

The AXParameter interface enables data and application settings to be saved so that they are not lost during a reboot or firmware upgrade of the Axis product.

Parameters specific to the application can be preconfigured when the application is created or be added in runtime.
In addition, AXParameter makes it possible to define callback functions that perform specific actions if a parameter is updated, for example if the user updates a parameter using the Axis product's web pages.

Data which only is used and altered by the application should not be handled by AXParameter. Such data can instead be handled by, for example, GLib GKeyFile.

Note that the parameters are not private to the application. The parameters can be read via VAPIX by a user with operator privilege, and they can be modified by a user with admin privilege. The parameters can also be modified from another application if the application users belongs to the same group, e.g. two applications running as dynamic users.

## Details description

AXParameter and its associated functions. Contains functions for adding, removing, setting and getting parameters. The interface supports registering callback functions when a parameter value is updated.

**Parameters**
For an application only one AXParameter instance should be created and shared globally.

Callback functions should avoid blocking calls, i.e. never call any axparameter method as this will very likely lead to a deadlock. In other words, there may only be one axparameter call at the time. A better way to handle updates of other parameters from a callback is to use a global data object that gets updated.

**Parameter Types**
These are all the parameter types possible to set with ax_parameter_add

| Name     | Type Format                                                                                                                                      | Example                                                        | Description                                                                                                                                                                                                                               |
|----------|--------------------------------------------------------------------------------------------------------------------------------------------------|----------------------------------------------------------------|-------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------|
| Enum     | `type = "enum:option1[|nice name1][,option2[|nice name2][,...]]"`                                                                               | `type = "enum:TCPIP|Generic TCP/IP,HTTP|Generic HTTP,OFF|OFF"`<br>`type = "enum:1|Low,10|Medium,30|High"` | Rendered as drop-down lists in the ACAP settings interface. Options are separated by commas, and if desired, a nice value can be provided after the `|` symbol. The nice value is what the user sees in the UI.                          |
| Bool     | `type = "bool:no,yes"`                                                                                                                           | `type = "bool:no,yes"`                                         | Rendered as a checkbox in the ACAP settings interface. The first value represents the unchecked state.                                                                                                                                     |
| String   | `type = "string[:maxlen=X]"`                                                                                                                     | `type = "string:maxlen=64"`                                    | Rendered as a plain text input field for strings. Optional maximum length (`maxlen`) can be added. All Unicode characters are supported.                                                                                                   |
| Int      | `type = "int[:[maxlen=X;][min=Y;][max=Z]]"`<br>`type = "int[:[maxlen=X;][range=[Y,][A-Z]]]"`<br>`type = "int"` | `type = "int:min=0"`<br>`type = "int:maxlen=3;min=0;max=100"`<br>`type = "int:maxlen=5;range=0,1024-65535"` | Rendered as a plain text input field for integers. Supports optional `maxlen`, `min`, `max`, and `range` parameters. Ranges can be comma-separated values or min-max spans, and both forms include the endpoints.                          |
| Password | `type = "password[:maxlen=X]"`                                                                                                                   | `type = "password:maxlen=64"`                                  | Rendered as a masked text input (`*`) for strings in the ACAP settings interface. Only readable via `axparameter`, not via `param.cgi`. Supports optional `maxlen`.                                                                      |


**Control Words**
When creating a new parameter with ax_parameter_add, it is also possible to prepend an extra control word, that will impact how the parameter is handled. Here is a list of the possible control words.

| Name      | Example                | Description                                                                                                               |
|-----------|------------------------|---------------------------------------------------------------------------------------------------------------------------|
| Hidden    | `type = "hidden:string"`   | Hides the parameter from the list of parameters in the ACAP settings interface.                                           |
| NoSync    | `type = "nosync:string"`   | Changes to the parameter are temporary and last only until the ACAP is restarted or the parameter is reloaded.            |
| ReadOnly  | `type = "readonly:string"` | The parameter cannot be modified using `axparameter` or `param.cgi`. It appears grayed out in the ACAP settings interface. |
